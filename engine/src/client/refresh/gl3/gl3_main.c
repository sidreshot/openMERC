/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2016-2017 Daniel Gibson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * Refresher setup and main part of the frame generation, for OpenGL3
 *
 * =======================================================================
 *
 * OpenMerc (package I4a): Soldier of Fortune renderer start-up, cvars,
 * clearing, projection and frame sequence (SPEC-413 §1, §5, §6; SPEC-410
 * World part of a frame; SPEC-412 §2, §10, §11). vid_gamma, vid_brightness
 * and vid_contrast build the image colour table at load time (SPEC-413 §9)
 * instead of a shader gamma.
 * Implements: SPEC-410, SPEC-412, SPEC-413
 */


#include "../ref_shared.h"
#include "header/local.h"

#define HANDMADE_MATH_IMPLEMENTATION
#include "header/HandmadeMath.h"

#define DG_DYNARR_IMPLEMENTATION
#include "header/DG_dynarr.h"

#ifdef YQ2_GL3_GLES3
  #define REF_VERSION "openMERC OpenGL ES3 renderer"
#else
  #define REF_VERSION "openMERC OpenGL3 renderer"
#endif

#ifndef OM_SOFREF
refimport_t ri; /* the SoF module defines it in gl3_sof.c */
#endif

gl3config_t gl3config;
gl3state_t gl3state;

unsigned gl3_rawpalette[256];

gl3model_t *gl3_worldmodel;

float gl3depthmin=0.0f, gl3depthmax=1.0f;

cplane_t frustum[4];

/* view origin */
vec3_t vup;
vec3_t vpn;
vec3_t vright;
vec3_t gl3_origin;

int gl3_visframecount; /* bumped when going to a new PVS */
int gl3_framecount; /* used for dlight push checking */

int c_brush_polys, c_alias_polys;

static float v_blend[4]; /* final blending color */

int gl3_viewcluster, gl3_viewcluster2, gl3_oldviewcluster, gl3_oldviewcluster2;

const hmm_mat4 gl3_identityMat4 = {{
		{1, 0, 0, 0},
		{0, 1, 0, 0},
		{0, 0, 1, 0},
		{0, 0, 0, 1},
}};

cvar_t *gl_msaa_samples;
cvar_t *r_vsync;
cvar_t *r_retexturing;
cvar_t *r_scale8bittextures;
cvar_t *vid_fullscreen;
cvar_t *r_mode;
cvar_t *r_customwidth;
cvar_t *r_customheight;
cvar_t *vid_gamma;
cvar_t *gl_anisotropic;
cvar_t *gl_texturemode;
cvar_t *gl_drawbuffer;
cvar_t *r_clear;
cvar_t *gl3_particle_size;
cvar_t *gl3_particle_fade_factor;
cvar_t *gl3_particle_square;
cvar_t *gl3_colorlight;
cvar_t *gl_polyblend;

cvar_t *gl_lefthand;
cvar_t *r_gunfov;
cvar_t *r_farsee;

cvar_t *gl3_intensity;
cvar_t *gl3_intensity_2D;
cvar_t *r_lightlevel;
cvar_t *gl3_overbrightbits;

cvar_t *r_norefresh;
cvar_t *r_drawentities;
cvar_t *r_drawworld;
cvar_t *gl_nolerp_list;
cvar_t *r_lerp_list;
cvar_t *r_2D_unfiltered;
cvar_t *r_videos_unfiltered;
cvar_t *gl_nobind;
cvar_t *gl_showtris;
cvar_t *r_lockpvs;
cvar_t *r_novis;
cvar_t *r_speeds;
cvar_t *gl_finish;

cvar_t *r_cull;
cvar_t *gl_zfix;
cvar_t *r_fullbright;
cvar_t *r_modulate;
cvar_t *gl_lightmap;
cvar_t *gl_shadows;
cvar_t *gl3_debugcontext;
cvar_t *r_fixsurfsky;
cvar_t *r_palettedtexture;
cvar_t *r_validation;
cvar_t *gl3_usefbo;

cvar_t *gl3_show_draw_stats;

static cvar_t *gl_znear;

DA_TYPEDEF(gl3_3D_vtx_t, Vtx3DArray_t);
DA_TYPEDEF(GLushort, UShortArray_t);
DA_TYPEDEF(gl3drawCmd_t, DrawCommandArray_t);
DA_TYPEDEF(hmm_mat4, Mat4Array_t);
// dynamic arrays to batch all consecutive 3D draws (with gl3_3D_vtx_t) with same texture to reduce drawcalls
static Vtx3DArray_t vtxBuf = {0};
static UShortArray_t idxBuf = {0};
static DrawCommandArray_t drawCmds = {0};
static Mat4Array_t transModelMats = {0};

// Yaw-Pitch-Roll
// equivalent to R_z * R_y * R_x where R_x is the trans matrix for rotating around X axis for aroundXdeg
static hmm_mat4 rotAroundAxisZYX(float aroundZdeg, float aroundYdeg, float aroundXdeg)
{
	// Naming of variables is consistent with http://planning.cs.uiuc.edu/node102.html
	// and https://de.wikipedia.org/wiki/Roll-Nick-Gier-Winkel#.E2.80.9EZY.E2.80.B2X.E2.80.B3-Konvention.E2.80.9C
	float alpha = HMM_ToRadians(aroundZdeg);
	float beta = HMM_ToRadians(aroundYdeg);
	float gamma = HMM_ToRadians(aroundXdeg);

	float sinA = HMM_SinF(alpha);
	float cosA = HMM_CosF(alpha);
	// TODO: or sincosf(alpha, &sinA, &cosA); ?? (not a standard function)
	float sinB = HMM_SinF(beta);
	float cosB = HMM_CosF(beta);
	float sinG = HMM_SinF(gamma);
	float cosG = HMM_CosF(gamma);

	hmm_mat4 ret = {{
		{ cosA*cosB,                  sinA*cosB,                   -sinB,    0 }, // first *column*
		{ cosA*sinB*sinG - sinA*cosG, sinA*sinB*sinG + cosA*cosG, cosB*sinG, 0 },
		{ cosA*sinB*cosG + sinA*sinG, sinA*sinB*cosG - cosA*sinG, cosB*cosG, 0 },
		{  0,                          0,                          0,        1 }
	}};

	return ret;
}

void
GL3_RotateUni3DforEntity(entity_t *e)
{
	// angles: pitch (around y), yaw (around z), roll (around x)
	// rot matrices to be multiplied in order Z, Y, X (yaw, pitch, roll)
	hmm_mat4 transMat = rotAroundAxisZYX(e->angles[1], -e->angles[0], -e->angles[2]);

	for(int i=0; i<3; ++i)
	{
		transMat.Elements[3][i] = e->origin[i]; // set translation
	}

	gl3state.uni3DData.transModelMat4 = HMM_MultiplyMat4(gl3state.uni3DData.transModelMat4, transMat);

	GL3_UpdateUBO3D();
}

// if replaceTransModelMat is true, the existing value of drawCmd->transModelMat
// is ignored and it's replaced with the one calculated here (otherwise they're multiplied)
void
GL3_RotateForEntity(entity_t *e, gl3drawCmd_t* drawCmd)
{
	// TODO: shortcut for "not rotated at all"?

	// angles: pitch (around y), yaw (around z), roll (around x)
	// rot matrices to be multiplied in order Z, Y, X (yaw, pitch, roll)
	hmm_mat4 transMat = rotAroundAxisZYX(e->angles[1], -e->angles[0], -e->angles[2]);

	for(int i=0; i<3; ++i)
	{
		transMat.Elements[3][i] = e->origin[i]; // set translation
	}

	GL3_SetDrawCmdTransMatrix(drawCmd, transMat);
}


static void
GL3_Strings(qboolean show_ext)
{
	void (*Printf_Ext)(const char *msg, ...) =
		(show_ext) ? Com_Printf : Com_DPrintf ;
	GLint i, numExtensions;

	/* SPEC-413: label in text colour 7, value in colour 1 */
	Com_Printf("\x07GL_VENDOR: \x01%s\n", gl3config.vendor_string);
	Com_Printf("\x07GL_RENDERER: \x01%s\n", gl3config.renderer_string);
	Com_Printf("\x07GL_VERSION: \x01%s\n", gl3config.version_string);

	glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);

	Printf_Ext("\x07GL_EXTENSIONS: \x01");
	for(i = 0; i < numExtensions; i++)
	{
		Printf_Ext(" %s", (const char*)glGetStringi(GL_EXTENSIONS, i));
	}
	Printf_Ext("\n");
}

static void
GL3_Strings_f(void)
{
	GL3_Strings(true);
}

#ifdef OM_SOFREF
/* renderer-private cvars of the SoF module (SPEC-002 hosts know only the SoF cvars) */
#define MAX_LOCAL_CVARS 64
static cvar_t localcvars[MAX_LOCAL_CVARS];
static char localstrings[MAX_LOCAL_CVARS][2][MAX_QPATH];
static int numlocalcvars;
#endif

cvar_t *
GL3_LocalCvar(const char *name, const char *value, int flags)
{
#ifdef OM_SOFREF
	int i;

	for (i = 0; i < numlocalcvars; i++)
	{
		if (!strcmp(localcvars[i].name, name))
		{
			return &localcvars[i];
		}
	}

	if (numlocalcvars == MAX_LOCAL_CVARS)
	{
		Com_Error(ERR_FATAL, "GL3_LocalCvar: too many");
	}

	i = numlocalcvars++;
	Q_strlcpy(localstrings[i][0], name, MAX_QPATH);
	Q_strlcpy(localstrings[i][1], value, MAX_QPATH);
	memset(&localcvars[i], 0, sizeof(localcvars[i]));
	localcvars[i].name = localstrings[i][0];
	localcvars[i].string = localstrings[i][1];
	localcvars[i].flags = flags;
	localcvars[i].value = (float)atof(value);
	localcvars[i].modified = true;
	return &localcvars[i];
#else
	return ri.Cvar_Get(name, (char *)value, flags);
#endif
}

static void
GL3_Register(void)
{
	/* OpenMerc: the Soldier of Fortune renderer cvars (SPEC-413) */
	GL3_SoF_RegisterCvars();

	/* Yamagi renderer settings without a SoF counterpart */
	gl_lefthand = GL3_LocalCvar("hand", "0", CVAR_USERINFO | CVAR_ARCHIVE);
	r_gunfov = GL3_LocalCvar("r_gunfov", "80", CVAR_ARCHIVE);
	r_farsee = GL3_LocalCvar("r_farsee", "1", CVAR_LATCH | CVAR_ARCHIVE); /* OpenMerc: max draw distance by default */
	gl_msaa_samples = GL3_LocalCvar("r_msaa_samples", "8", CVAR_ARCHIVE); /* OpenMerc: max MSAA default; driver may clamp */
	r_retexturing = GL3_LocalCvar("r_retexturing", "1", CVAR_ARCHIVE);
	r_scale8bittextures = GL3_LocalCvar("r_scale8bittextures", "0", CVAR_ARCHIVE);
	gl3_debugcontext = GL3_LocalCvar("gl3_debugcontext", "0", 0);
	r_customwidth = GL3_LocalCvar("r_customwidth", "1024", CVAR_ARCHIVE);
	r_customheight = GL3_LocalCvar("r_customheight", "768", CVAR_ARCHIVE);
	gl3_particle_size = GL3_LocalCvar("gl3_particle_size", "40", CVAR_ARCHIVE);
	gl3_particle_fade_factor = GL3_LocalCvar("gl3_particle_fade_factor", "1.2", CVAR_ARCHIVE);
	gl3_particle_square = GL3_LocalCvar("gl3_particle_square", "0", CVAR_ARCHIVE);
	gl3_colorlight = GL3_LocalCvar("gl3_colorlight", "1", CVAR_ARCHIVE);
	gl_polyblend = GL3_LocalCvar("gl_polyblend", "1", CVAR_ARCHIVE);
	r_fixsurfsky = GL3_LocalCvar("r_fixsurfsky", "0", CVAR_ARCHIVE);
	r_palettedtexture = GL3_LocalCvar("r_palettedtexture", "0", 0);
	r_validation = GL3_LocalCvar("r_validation", "0", CVAR_ARCHIVE);
	gl_nolerp_list = GL3_LocalCvar("r_nolerp_list", "pics/conchars.pcx pics/ch1.pcx pics/ch2.pcx pics/ch3.pcx", CVAR_ARCHIVE);
	r_lerp_list = GL3_LocalCvar("r_lerp_list", "", CVAR_ARCHIVE);
	r_2D_unfiltered = GL3_LocalCvar("r_2D_unfiltered", "0", CVAR_ARCHIVE);
	r_videos_unfiltered = GL3_LocalCvar("r_videos_unfiltered", "0", CVAR_ARCHIVE);
	/* OpenMerc: 0 = SoF rule (gl_ext_anisotropic: 2x), 1 = off, N = N x; default 16 = max (clamped to GPU max) */
	gl_anisotropic = GL3_LocalCvar("r_anisotropic", "16", CVAR_ARCHIVE);
	gl3_intensity = GL3_LocalCvar("gl3_intensity", "1", CVAR_ARCHIVE);
	gl3_intensity_2D = GL3_LocalCvar("gl3_intensity_2D", "1", CVAR_ARCHIVE);
	gl3_overbrightbits = GL3_LocalCvar("gl3_overbrightbits", "1", CVAR_ARCHIVE);
	r_modulate = GL3_LocalCvar("r_modulate", "1", CVAR_ARCHIVE);
	gl_shadows = GL3_LocalCvar("r_shadows", "0", CVAR_ARCHIVE);
	gl_zfix = GL3_LocalCvar("gl_zfix", "0", 0);
	gl3_usefbo = GL3_LocalCvar("gl3_usefbo", "0", CVAR_ARCHIVE);
	gl3_show_draw_stats = GL3_LocalCvar("gl3_show_draw_stats", "0", CVAR_ARCHIVE);

	/* SoF names (SPEC-413) */
	gl_drawbuffer = ri.Cvar_Get("gl_drawbuffer", "GL_BACK", 0);
#ifdef OM_SOFREF
	r_vsync = gl_swapinterval;
	r_mode = gl_mode;
#else
	/* the OpenMerc engine's video code still uses the Yamagi names;
	   V1: r_mode -2 = the desktop resolution is the default (docs/DEVIATIONS.md) */
	r_vsync = ri.Cvar_Get("r_vsync", "1", CVAR_ARCHIVE);
	r_mode = ri.Cvar_Get("r_mode", "-2", CVAR_ARCHIVE);
#endif
	r_norefresh = ri.Cvar_Get("r_norefresh", "0", 0);
	r_drawentities = ri.Cvar_Get("r_drawentities", "1", 0);
	r_drawworld = ri.Cvar_Get("r_drawworld", "1", 0);
	r_fullbright = ri.Cvar_Get("r_fullbright", "0", 0);
	gl_nobind = ri.Cvar_Get("gl_nobind", "0", 0);
	gl_showtris = ri.Cvar_Get("gl_showtris", "0", 0);
	gl_texturemode = ri.Cvar_Get("gl_texturemode", "GL_LINEAR_MIPMAP_LINEAR", CVAR_ARCHIVE); /* OpenMerc: trilinear max default */
	vid_fullscreen = ri.Cvar_Get("vid_fullscreen", "0", CVAR_ARCHIVE);
	vid_gamma = ri.Cvar_Get("vid_gamma", "0.0", CVAR_ARCHIVE);
	r_lightlevel = ri.Cvar_Get("r_lightlevel", "0", 0);
	gl_lightmap = ri.Cvar_Get("gl_lightmap", "0", 0);
	r_clear = gl_clear;
	r_cull = ri.Cvar_Get("gl_cull", "1", 0);
	r_lockpvs = gl_lockpvs;
	r_novis = ri.Cvar_Get("r_novis", "0", 0);
	r_speeds = ri.Cvar_Get("r_speeds", "0", 0);
	gl_finish = ri.Cvar_Get("gl_finish", "0", 0);
	gl_znear = r_nearclipdist;

	ri.Cmd_AddCommand("imagelist", GL3_ImageList_f);
	ri.Cmd_AddCommand("screenshot", GL3_SoF_ScreenShot_f);
	ri.Cmd_AddCommand("rawshot", GL3_SoF_RawShot_f);
	ri.Cmd_AddCommand("avi", GL3_Avi_f); /* I4c: SPEC-412 §13 */
	ri.Cmd_AddCommand("gl_strings", GL3_Strings_f);
#ifndef OM_SOFREF
	ri.Cmd_AddCommand("modellist", GL3_Mod_Modellist_f);
#endif
}

/*
 * Changes the video mode
 */

// the following is only used in the next to functions,
// no need to put it in a header
enum
{
	rserr_ok,

	rserr_invalid_mode,

	rserr_unknown
};

static int
SetMode_impl(int *pwidth, int *pheight, int mode, int fullscreen)
{
	Com_Printf("Setting mode %d:", mode);

	/* mode -1 is not in the vid mode table - so we keep the values in pwidth
	   and pheight and don't even try to look up the mode info */
	if ((mode >= 0) && !ri.Vid_GetModeInfo(pwidth, pheight, mode))
	{
		Com_Printf(" invalid mode\n");
		return rserr_invalid_mode;
	}

	/* We trying to get resolution from desktop */
	if (mode == -2)
	{
		if(!ri.GLimp_GetDesktopMode(pwidth, pheight))
		{
			Com_Printf(" can't detect mode\n" );
			return rserr_invalid_mode;
		}
	}

	Com_Printf(" %dx%d (vid_fullscreen %i)\n", *pwidth, *pheight, fullscreen);

	if (!ri.GLimp_InitGraphics(fullscreen, pwidth, pheight))
	{
		return rserr_invalid_mode;
	}

	if (mode == -2 || fullscreen)
	{
		GL3_BindVBO(0);
	}

	/* This is totally obscure: For some strange reasons the renderer
	   maintains two(!) representations of the resolution. One comes
	   from the client and is saved in r_newrefdef. The other one
	   is determined here and saved in vid. Several calculations take
	   both representations into account.

	   The values will always be the same. The GLimp_InitGraphics()
	   call above communicates the requested resolution to the client
	   where it ends up in the vid subsystem and the vid system writes
	   it into r_newrefdef.

	   We can't avoid the client roundtrip, because we can get the
	   real size of the drawable (which can differ from the resolution
	   due to high dpi awareness) only after the render context was
	   created by GLimp_InitGraphics() and need to communicate it
	   somehow to the client. So we just overwrite the values saved
	   in vid with a call to GL3_GetDrawableSize(), just like the
	   client does. This makes sure that both values are the same
	   and everything is okay.

	   We also need to take the special case fullscreen window into
	   account. With the fullscreen windows we cannot use the
	   drawable size, it would scale all cases to the size of the
	   window. Instead use the drawable size when the user wants
	   native resolution (the fullscreen window fills the screen)
	   and use the requested resolution in all other cases. */
	if (IsHighDPIaware)
	{
		if (vid_fullscreen->value != 2)
		{
			GL3_GetDrawableSize(pwidth, pheight);
		}
		else
		{
			if (r_mode->value == -2)
			{
				/* User requested native resolution. */
				GL3_GetDrawableSize(pwidth, pheight);
			}
		}
	}

	return rserr_ok;
}

static qboolean
GL3_SetMode(void)
{
	int err;
	int fullscreen;

	fullscreen = (int)vid_fullscreen->value;

	/* OpenMerc: SPEC-413 §3 step 2 - the modified flags of the mode cvars are cleared */
	r_mode->modified = false;
	vid_fullscreen->modified = false;

	/* a bit hackish approach to enable custom resolutions:
	   Glimp_SetMode needs these values set for mode -1 */
	vid.width = r_customwidth->value;
	vid.height = r_customheight->value;

	if ((err = SetMode_impl(&vid.width, &vid.height, r_mode->value, fullscreen)) == rserr_ok)
	{
		if (r_mode->value == -1)
		{
			gl3state.prev_mode = 4; /* safe default for custom mode */
		}
		else
		{
			gl3state.prev_mode = r_mode->value;
		}
	}
	else
	{
		if (err == rserr_invalid_mode)
		{
			Com_Printf("ref_gl3::GL3_SetMode() - invalid mode\n");

			if (gl_msaa_samples->value != 0.0f)
			{
				Com_Printf("gl_msaa_samples was %d - will try again with gl_msaa_samples = 0\n", (int)gl_msaa_samples->value);
				ri.Cvar_SetValue("r_msaa_samples", 0.0f);
				gl_msaa_samples->modified = false;

				if ((err = SetMode_impl(&vid.width, &vid.height, r_mode->value, 0)) == rserr_ok)
				{
					return true;
				}
			}
			if(r_mode->value == gl3state.prev_mode)
			{
				// trying again would result in a crash anyway, give up already
				// (this would happen if your initing fails at all and your resolution already was 640x480)
				return false;
			}

			ri.Cvar_SetValue("r_mode", gl3state.prev_mode);
			r_mode->modified = false;
		}

		/* try setting it back to something safe */
		if ((err = SetMode_impl(&vid.width, &vid.height, gl3state.prev_mode, 0)) != rserr_ok)
		{
			Com_Printf("ref_gl3::GL3_SetMode() - could not revert to safe mode\n");
			return false;
		}
	}

	return true;
}

static void
GL3_ResetClearColor(void)
{
	/* SPEC-413 §5: default clear colour (1, 0, 0.5, 0.5) */
	glClearColor(1, 0, 0.5, 0.5);
}

static qboolean
HasExtension(const char *name)
{
	GLint i, n = 0;

	glGetIntegerv(GL_NUM_EXTENSIONS, &n);

	for (i = 0; i < n; i++)
	{
		const char *e = (const char *)glGetStringi(GL_EXTENSIONS, i);

		if (e && !strcmp(e, name))
		{
			return true;
		}
	}

	return false;
}

static qboolean
GL3_Init(void)
{
	byte *colormap;
	GLint maxtex = 0;

	Swap_Init(); // FIXME: for fucks sake, this doesn't have to be done at runtime!

	Com_Printf("ref_gl version: " REF_VERSION "\n");

	if(sizeof(float) != sizeof(GLfloat))
	{
		// if this ever happens, things would explode because we feed vertex arrays and UBO data
		// using floats to OpenGL, which expects GLfloat (can't easily change, those floats are from HMM etc)
		// (but to be honest I very much doubt this will ever happen.)
		Com_Printf("ref_gl3: sizeof(float) != sizeof(GLfloat) - we're in real trouble here.\n");
		return false;
	}

	GetPCXPalette (&colormap, d_8to24table);
	free(colormap);

	da_clear(transModelMats);
	da_add(transModelMats, gl3_identityMat4); // element 0 is always the identity matrix

	GL3_Register();

	/* set our "safe" mode */
	gl3state.prev_mode = 3;

	/* create the window and set up the context */
	if (!GL3_SetMode())
	{
		Com_Printf("ref_gl::R_Init() - could not R_SetMode()\n");
		return false;
	}

#ifndef OM_SOFREF
	ri.Vid_MenuInit();
#endif

	/* get our various GL strings */
	gl3config.vendor_string = (const char*)glGetString(GL_VENDOR);
	gl3config.renderer_string = (const char*)glGetString(GL_RENDERER);
	gl3config.version_string = (const char*)glGetString(GL_VERSION);
	gl3config.glsl_version_string = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);

	/* SPEC-413 §1 step 7: printed and copied into cvars of the same lower-case name */
	ri.Cvar_Set("gl_vendor", (char *)gl3config.vendor_string);
	ri.Cvar_Set("gl_renderer", (char *)gl3config.renderer_string);
	ri.Cvar_Set("gl_version", (char *)gl3config.version_string);
	Com_Printf("GL_VENDOR: %s\n", gl3config.vendor_string);
	Com_Printf("GL_RENDERER: %s\n", gl3config.renderer_string);
	Com_Printf("GL_VERSION: %s\n", gl3config.version_string);
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxtex);
	Com_Printf("GL_MAX_TEXTURE_SIZE: %d\n", maxtex);

	/* SPEC-413 §1 step 9 */
	Com_Printf("...allowing CDS\n");
	Com_Printf("...enabling WGL_EXT_swap_control\n");

	if (gl_ext_multitexture->value)
	{
		GLint tmus = 0;

		glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &tmus);
		Com_Printf("...using GL_ARB_multitexture (%d TMUs)\n", tmus);
	}
	else
	{
		/* the shader path always combines texture and lightmap in one pass */
		Com_Printf("...ignoring GL_ARB_multitexture\n");
	}

	if(gl3config.anisotropic)
	{
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &gl3config.max_anisotropy);
		Com_Printf("...GL_EXT_texture_filter_anisotropic available\n");
		Com_Printf(gl_ext_anisotropic->value ? "...using GL_EXT_texture_filter_anisotropic\n" :
			"...ignoring GL_EXT_texture_filter_anisotropic\n");
	}
	else
	{
		gl3config.max_anisotropy = 0.0;
		Com_Printf("...GL_EXT_texture_filter_anisotropic not found\n");
	}

	gl3config.s3tc = HasExtension("GL_EXT_texture_compression_s3tc");

	if (gl3config.s3tc && gl_s3_s3tc->value)
	{
		ri.Cvar_Set("s3tc_avail", "1");
		Com_Printf("...using GL_S3_s3tc\n");
	}
	else
	{
		ri.Cvar_Set("s3tc_avail", "0");

		if (gl3config.s3tc)
		{
			Com_Printf("...ignoring GL_S3_s3tc\n");
		}
	}

	if(gl3config.debug_output && gl3_debugcontext->value != 0.0f)
	{
		Com_Printf(" - OpenGL Debug Output: enabled with gl3_debugcontext = %i\n", (int)gl3_debugcontext->value);
	}

	// generate texture handles for all possible lightmaps
	glGenTextures(MAX_LIGHTMAPS*MAX_LIGHTMAPS_PER_SURFACE, gl3state.lightmap_textureIDs[0]);

	GL3_ResetClearColor();
	GL3_SetDefaultState();

	if(GL3_InitShaders())
	{
		Com_DPrintf("Loading shaders succeeded.\n");
	}
	else
	{
		Com_Printf("Loading shaders failed.\n");
		return false;
	}

	registration_sequence = 1; // from R_InitImages() (everything else from there shouldn't be needed anymore)

	/* SPEC-413 §1 step 10: image system with gl_modulate latched and the colour table */
	gl3_modulate_latched = gl_modulate->value;
	GL3_SoF_BuildColorTable(vid_gamma->value, vid_brightness->value, vid_contrast->value, 1.0f);
	GL3_SoF_InitImages();

	GL3_Scrap_Init();

	GL3_Mod_Init();

	GL3_InitParticleTexture();

	GL3_Draw_InitLocal();

	GL3_SurfInit();

	GL3_SoF2D_Init();

	GL3_FX_Init();

	GL3_Post_Init(); /* GFX-modern: bloom and soft particles */

	GL3_CSM_Init(); /* GFX-modern: cascaded sun shadows */

	GL3_Liquid_Init(); /* I4c: SPEC-413 §1 step 1, the warp table is halved at every start */

	/* default images and fonts (a missing one is fatal) */
	if (!GL3_SoF_FindImage("pics/console/notexture", SOFIMG_PIC, true))
	{
		Com_Error(ERR_FATAL, "Couldn't load pics/console/notexture");
	}

	GL3_SoF2D_InitFonts();

	glGenFramebuffers(1, &gl3state.ppFBO);
	// the rest for the FBO is done dynamically in GL3_RenderView() so it can
	// take the viewsize into account (enforce that by setting invalid size)
	gl3state.ppFBtexWidth = gl3state.ppFBtexHeight = -1;

	/* one full colour and depth clear */
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GL3_ResetClearColor();

	return true;
}

void
GL3_Shutdown(void)
{
	ri.Cmd_RemoveCommand("screenshot");
	ri.Cmd_RemoveCommand("rawshot");
	ri.Cmd_RemoveCommand("avi");
	GL3_Avi_Shutdown(); /* I4c: shutdown also stops a recording */
	ri.Cmd_RemoveCommand("imagelist");
	ri.Cmd_RemoveCommand("gl_strings");
#ifndef OM_SOFREF
	ri.Cmd_RemoveCommand("modellist");
#endif

	// only call all these if we have an OpenGL context and the gl function pointers
	// randomly chose one function that should always be there to test..
	if(glDeleteBuffers != NULL)
	{
		GL3_Mod_FreeAll();
		GL3_Fog_FreeRegions();
		GL3_Terrain_Free(); /* I4c: lives until the library is unloaded (its images go with GL3_ShutdownImages) */
		GL3_DynLight_Free(); /* I4c */
		GL3_ShutdownMeshes();
		GL3_SoF2D_Shutdown();
		GL3_FX_Shutdown();
		GL3_Post_Shutdown();
		GL3_CSM_Shutdown();
		GL3_ShutdownImages();
		GL3_SurfShutdown();
		GL3_Draw_ShutdownLocal();
		GL3_ShutdownShaders();

		// free the postprocessing FBO and its renderbuffer and texture
		if(gl3state.ppFBrbo != 0)
			glDeleteRenderbuffers(1, &gl3state.ppFBrbo);
		if(gl3state.ppFBtex != 0)
			glDeleteTextures(1, &gl3state.ppFBtex);
		if(gl3state.ppFBO != 0)
			glDeleteFramebuffers(1, &gl3state.ppFBO);
		gl3state.ppFBrbo = gl3state.ppFBtex = gl3state.ppFBO = 0;
		gl3state.ppFBObound = false;
		gl3state.ppFBtexWidth = gl3state.ppFBtexHeight = -1;
	}

	da_free(vtxBuf);
	da_free(idxBuf);
	da_free(drawCmds);
	da_free(transModelMats);

	/* shutdown OS specific OpenGL stuff like contexts, etc.  */
	GL3_ShutdownContext();
}

static inline qboolean
colorsEqual(const byte c1[3], const byte c2[3])
{
	return c1[0] == c2[0] && c1[1] == c2[1] && c1[2] == c2[2];
}

static inline qboolean
lmstylesEqual(const byte st1[MAXLIGHTMAPS], const byte st2[MAXLIGHTMAPS])
{
	for(int i=0; i<MAXLIGHTMAPS; ++i)
	{
		byte astyle = st1[i];
		if(astyle != st2[i])
			return false;
		// 255 indicates that this and the remaining styles can be ignored
		// (still needed to check first if a and b agree about ignoring it)
		if(astyle == 255)
			break;
	}
	return true;
}

static void
UpdateLMscales(const hmm_vec4 lmScales[MAX_LIGHTMAPS_PER_SURFACE], gl3ShaderInfo_t* si)
{
	int i;
	qboolean hasChanged = false;

	for(i=0; i<MAX_LIGHTMAPS_PER_SURFACE; ++i)
	{
		if(hasChanged)
		{
			si->lmScales[i] = lmScales[i];
		}
		else if(   si->lmScales[i].R != lmScales[i].R
		        || si->lmScales[i].G != lmScales[i].G
		        || si->lmScales[i].B != lmScales[i].B
		        || si->lmScales[i].A != lmScales[i].A )
		{
			si->lmScales[i] = lmScales[i];
			hasChanged = true;
		}
	}

	if(hasChanged)
	{
		glUniform4fv(si->uniLmScalesOrTime, MAX_LIGHTMAPS_PER_SURFACE, si->lmScales[0].Elements);
	}
}

void GL3_Draw3DBatchesNow()
{
	if(da_count(drawCmds) == 0)
		return;

	GL3_BindVAO(gl3state.vao3D);
	GL3_BindVBO(gl3state.vbo3D);
	GL3_BindEBO(gl3state.ebo3D);

	glBufferData(GL_ARRAY_BUFFER, da_count(vtxBuf)*sizeof(gl3_3D_vtx_t), vtxBuf.p, GL_STREAM_DRAW);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, da_count(idxBuf)*sizeof(GLushort), idxBuf.p, GL_STREAM_DRAW);

	++gl3_numBufferVtxData;

	// set curState to something that reflects the actual state, as far as possible,
	// and otherwise makes sure it'll be set in the loop
	gl3drawCmd_t curState = GL3_CreateDrawCmd();

	curState.texnum = gl3state.currenttexture;
	curState.lmtexnum = gl3state.currentlightmap;
	curState.alpha = gl3state.uni3DData.alpha;
	curState.scroll = gl3state.uni3DData.scroll;
	curState.lightScaleForTurb = gl3state.uni3DData.lightScaleForTurb;

	// for the next two the approach is setting a value that
	// is different from the one in the first drawCmd, to make sure
	// the corresponding state is set in the first iteration
	curState.flags = ~drawCmds.p[0].flags; // just the opposite flags of the first element
	curState.transModelMatIdx = drawCmds.p[0].transModelMatIdx + 1;

	gl3ShaderInfo_t* shader = NULL;

	for(int i=0, n=da_count(drawCmds); i < n; ++i)
	{
		gl3drawCmd_t* cmd = da_getptr(drawCmds, i);

		int flags = cmd->flags;
		int curFlags = curState.flags;
		qboolean updateUni3D = false;

		if(cmd->shaderIdx != curState.shaderIdx)
		{
			shader = GL3_GetDrawCmdShader(cmd);
			GL3_UseProgram( shader->shaderProgram );
		}
		if(cmd->texnum != gl3state.currenttexture)
			GL3_Bind(cmd->texnum);
		if(cmd->lmtexnum >= 0)
			GL3_BindLightmap(cmd->lmtexnum);

		if((flags & DCFlag_DisableDepthMask) != (curFlags & DCFlag_DisableDepthMask))
			glDepthMask((flags & DCFlag_DisableDepthMask) == 0);

		if((flags & DCFlag_Blend) != (curFlags & DCFlag_Blend))
		{
			if(flags & DCFlag_Blend)
				glEnable(GL_BLEND);
			else
				glDisable(GL_BLEND);
		}

		if((flags & DCFlag_PolyOffsetFill) != (curFlags & DCFlag_PolyOffsetFill))
		{
			if(flags & DCFlag_PolyOffsetFill)
				glEnable(GL_POLYGON_OFFSET_FILL);
			else
				glDisable(GL_POLYGON_OFFSET_FILL);
		}

		if((flags & DCFlag_UseScroll) && cmd->scroll != gl3state.uni3DData.scroll)
		{
			gl3state.uni3DData.scroll = cmd->scroll;
			updateUni3D = true;
		}

		/* OpenMerc (I4c): t scroll of SoF flowing surfaces; Quake II commands leave it 0 */
		if((flags & DCFlag_UseScroll) && cmd->scrollT != gl3state.uni3DData.scrollT)
		{
			gl3state.uni3DData.scrollT = cmd->scrollT;
			updateUni3D = true;
		}

		/* OpenMerc GFX-modern: bloom weight of SURF_LIGHT faces (gl3_post.c) */
		if(cmd->emissive != gl3state.uni3DData.emissive)
		{
			gl3state.uni3DData.emissive = cmd->emissive;
			updateUni3D = true;
		}

		/* OpenMerc GFX-modern: texture size of the face for per-pixel SoF dlights (r_om_pdlight) */
		if(cmd->texdim[0] != gl3state.uni3DData.omTexDim.X || cmd->texdim[1] != gl3state.uni3DData.omTexDim.Y)
		{
			gl3state.uni3DData.omTexDim.X = cmd->texdim[0];
			gl3state.uni3DData.omTexDim.Y = cmd->texdim[1];
			updateUni3D = true;
		}

		/* OpenMerc (I4c): linear fog of gl_fogmode 1 for faces in a fog region (SPEC-410) */
		if(flags & DCFlag_UseFog)
		{
			if(GL3_Fog_SetHardwareFog(cmd->fogregion, &gl3state.uni3DData))
				updateUni3D = true;
		}
		else if(gl3state.uni3DData.fogParams.Z != 0.0f)
		{
			gl3state.uni3DData.fogParams.Z = 0.0f;
			updateUni3D = true;
		}

		if((flags & DCFlag_UseLightScaleForTurb)
		   && cmd->lightScaleForTurb != gl3state.uni3DData.lightScaleForTurb)
		{
			gl3state.uni3DData.lightScaleForTurb = cmd->lightScaleForTurb;
			updateUni3D = true;
		}

		if((flags & DCFlag_UseLmStyles) && !lmstylesEqual(cmd->styles, curState.styles))
		{
			hmm_vec4 lmScales[MAX_LIGHTMAPS_PER_SURFACE] = {0};
			lmScales[0] = HMM_Vec4(1.0f, 1.0f, 1.0f, 1.0f);
			/* OpenMerc (I4c): gl_dynamic 0 keeps the lightmaps as built at load, where every style is (1, 1, 1)
			   (SPEC-410 Lightmaps, World diagnostics) */
			qboolean frozen = gl_dynamic && !gl_dynamic->value && gl3_worldmodel &&
				(gl3_worldmodel->bspversion == BSPVERSION_SOF);
			for(int map = 0; map < MAX_LIGHTMAPS_PER_SURFACE && cmd->styles[map] != 255; map++)
			{
				/* OpenMerc, SPEC-410: static light is multiplied by the latched gl_modulate */
				const float *rgb = r_newrefdef.lightstyles[cmd->styles[map]].rgb;
				lmScales[map].R = (frozen ? 1.0f : rgb[0]) * gl3_modulate_latched;
				lmScales[map].G = (frozen ? 1.0f : rgb[1]) * gl3_modulate_latched;
				lmScales[map].B = (frozen ? 1.0f : rgb[2]) * gl3_modulate_latched;
				lmScales[map].A = 1.0f;
			}
			UpdateLMscales(lmScales, shader);
		}

		if(flags & DCFlag_UseColor)
		{
			if(cmd->alpha != curState.alpha || !colorsEqual(cmd->color, curState.color))
			{
				gl3state.uniCommonData.color.R = cmd->color[0] * (1.0f/255.0f);
				gl3state.uniCommonData.color.G = cmd->color[1] * (1.0f/255.0f);
				gl3state.uniCommonData.color.B = cmd->color[2] * (1.0f/255.0f);
				gl3state.uniCommonData.color.A = cmd->alpha;
				GL3_UpdateUBOCommon();
			}
		}
		else if((flags & DCFlag_Blend) && cmd->alpha != gl3state.uni3DData.alpha)
		{
			gl3state.uni3DData.alpha = cmd->alpha;
			updateUni3D = true;
		}

		if(cmd->transModelMatIdx != curState.transModelMatIdx)
		{
			gl3state.uni3DData.transModelMat4 = da_get(transModelMats, cmd->transModelMatIdx);
			updateUni3D = true;
		}

		if(updateUni3D)
			GL3_UpdateUBO3D();

		uintptr_t elemOffset = cmd->idxBufOffset * sizeof(GLushort);
		glDrawElements(GL_TRIANGLES, cmd->numElements, GL_UNSIGNED_SHORT, (void*)elemOffset);
		curState = *cmd;

		++gl3_num3Ddraws;
	}

	da_clear(vtxBuf);
	da_clear(idxBuf);
	da_clear(drawCmds);

	da_setcount(transModelMats, 1); // keep index 0 (identity matrix)

	// restore sane default for other draw operations (models, particles, 2D)
	if(curState.transModelMatIdx != 0 || gl3state.uni3DData.fogParams.Z != 0.0f || gl3state.uni3DData.emissive != 0.0f)
	{
		gl3state.uni3DData.transModelMat4 = gl3_identityMat4;
		gl3state.uni3DData.emissive = 0.0f;
		gl3state.uni3DData.fogParams.Z = 0.0f; /* OpenMerc (I4c): no gl_fogmode 1 fog outside its faces */
		GL3_UpdateUBO3D();
	}
	int curFlags = curState.flags;
	if(curFlags & DCFlag_Blend)
		glDisable(GL_BLEND);
	if(curFlags & DCFlag_DisableDepthMask)
		glDepthMask(GL_TRUE);

	if(curFlags & DCFlag_PolyOffsetFill)
		glDisable(GL_POLYGON_OFFSET_FILL);
}

static qboolean drawStateEqual(const gl3drawCmd_t* a, const gl3drawCmd_t* b)
{
	if( a->flags != b->flags || a->shaderIdx != b->shaderIdx
	   || a->texnum != b->texnum || a->lmtexnum != b->lmtexnum
	   || a->transModelMatIdx != b->transModelMatIdx )
		return false;

	int flags = a->flags; // at this point we know the flags are identical

	if((flags & DCFlag_UseScroll) && (a->scroll != b->scroll || a->scrollT != b->scrollT))
		return false;

	if((flags & DCFlag_UseFog) && a->fogregion != b->fogregion)
		return false;

	if((flags & DCFlag_UseLightScaleForTurb) && a->lightScaleForTurb != b->lightScaleForTurb)
		return false;

	if( (flags & DCFlag_UseColor) && !colorsEqual(a->color, b->color) )
		return false;

	if(a->alpha != b->alpha || a->emissive != b->emissive)
		return false;

	if(a->texdim[0] != b->texdim[0] || a->texdim[1] != b->texdim[1])
		return false;

	if((flags & DCFlag_UseLmStyles) && !lmstylesEqual(a->styles, b->styles))
		return false;

	return true;
}

void
GL3_SetDrawCmdTransMatrix(gl3drawCmd_t* drawCmd, hmm_mat4 mat)
{
	int idx = da_count(transModelMats);
	da_add(transModelMats, mat);
	drawCmd->transModelMatIdx = idx;
}

// assumes gl3state.v[ab]o3D are bound
// buffers and draws gl3_3D_vtx_t vertices
// drawMode is something like GL_TRIANGLE_STRIP or GL_TRIANGLE_FAN or whatever
void
GL3_Add3DdrawCmdToBatch(const gl3_3D_vtx_t* verts, int numVerts, GLenum drawMode, gl3drawCmd_t drawCmd)
{
	if(numVerts > UINT16_MAX)
	{
		Com_Printf("WARNING: Discarding a draw command with %d vertices (max %d allowed)!\n", numVerts, UINT16_MAX);
		return;
	}

	if(da_count(vtxBuf)+numVerts > UINT16_MAX)
	{
		GL3_Draw3DBatchesNow();
	}

	GLushort nextVtxIdx = da_count(vtxBuf);
	drawCmd.idxBufOffset = da_count(idxBuf);
	assert(drawCmd.shaderIdx != -1);

	// translate triangle fan/strip to just triangle indices
	if(drawMode == GL_TRIANGLE_FAN)
	{
		for(GLushort i=1; i < numVerts-1; ++i)
		{
			GLushort* add = da_addn_uninit(idxBuf, 3);

			add[0] = nextVtxIdx;
			add[1] = nextVtxIdx+i;
			add[2] = nextVtxIdx+i+1;
		}
	}
	else if(drawMode == GL_TRIANGLE_STRIP)
	{
		GLushort i;
		for(i=1; i < numVerts-2; i+=2)
		{
			// add two triangles at once, because the vertex order is different
			// for odd vs even triangles
			GLushort* add = da_addn_uninit(idxBuf, 6);

			add[0] = nextVtxIdx + i-1;
			add[1] = nextVtxIdx + i;
			add[2] = nextVtxIdx + i+1;

			add[3] = nextVtxIdx + i;
			add[4] = nextVtxIdx + i+2;
			add[5] = nextVtxIdx + i+1;
		}
		// add remaining triangle, if any
		if(i < numVerts-1)
		{
			GLushort* add = da_addn_uninit(idxBuf, 3);

			add[0] = nextVtxIdx + i-1;
			add[1] = nextVtxIdx + i;
			add[2] = nextVtxIdx + i+1;
		}
	}
	else if(drawMode == GL_TRIANGLES)
	{
		GLushort* add = da_addn_uninit(idxBuf, numVerts);
		for(GLushort i=0; i<numVerts; ++i)
			add[i] = nextVtxIdx + i;
	}
	else
	{
		Com_Printf("GL3_BufferAndDraw3D(): WARNING: Unsupported drawmode 0x%x\n", drawMode);
		return;
	}

	da_addn(vtxBuf, verts, numVerts);

	int numAddedIndices = da_count(idxBuf) - drawCmd.idxBufOffset;

	gl3drawCmd_t* lastDrawCmd = da_lastptr(drawCmds);
	if(lastDrawCmd != NULL && drawStateEqual(lastDrawCmd, &drawCmd))
	{
		lastDrawCmd->numElements += numAddedIndices;
	}
	else
	{
		drawCmd.numElements = numAddedIndices;
		da_add(drawCmds, drawCmd);
	}
}

static void
GL3_DrawBeam(entity_t *e)
{
	int i;
	enum { NUM_BEAM_SEGS = 6 };

	gl3drawCmd_t drawCmd = GL3_CreateDrawCmd();

	vec3_t perpvec;
	vec3_t direction, normalized_direction;
	vec3_t start_points[NUM_BEAM_SEGS], end_points[NUM_BEAM_SEGS];
	vec3_t oldorigin, origin;

	gl3_3D_vtx_t verts[NUM_BEAM_SEGS*4];
	unsigned int pointb;

	oldorigin[0] = e->oldorigin[0];
	oldorigin[1] = e->oldorigin[1];
	oldorigin[2] = e->oldorigin[2];

	origin[0] = e->origin[0];
	origin[1] = e->origin[1];
	origin[2] = e->origin[2];

	normalized_direction[0] = direction[0] = oldorigin[0] - origin[0];
	normalized_direction[1] = direction[1] = oldorigin[1] - origin[1];
	normalized_direction[2] = direction[2] = oldorigin[2] - origin[2];

	if (VectorNormalize(normalized_direction) == 0)
	{
		return;
	}

	PerpendicularVector(perpvec, normalized_direction);
	VectorScale(perpvec, e->frame / 2, perpvec);

	for (i = 0; i < 6; i++)
	{
		RotatePointAroundVector(start_points[i], normalized_direction, perpvec,
		                        (360.0 / NUM_BEAM_SEGS) * i);
		VectorAdd(start_points[i], origin, start_points[i]);
		VectorAdd(start_points[i], direction, end_points[i]);
	}

	//glDisable(GL_TEXTURE_2D);
	drawCmd.flags |= (DCFlag_Blend | DCFlag_DisableDepthMask | DCFlag_UseColor);

	GL3_SetDrawCmdShader(&drawCmd, &gl3state.si3DcolorOnly);

	drawCmd.color[0] = (LittleLong(d_8to24table[e->skinnum & 0xFF])) & 0xFF;
	drawCmd.color[1] = (LittleLong(d_8to24table[e->skinnum & 0xFF]) >> 8) & 0xFF;
	drawCmd.color[2] = (LittleLong(d_8to24table[e->skinnum & 0xFF]) >> 16) & 0xFF;
	drawCmd.alpha = e->alpha;



	for ( i = 0; i < NUM_BEAM_SEGS; i++ )
	{
		VectorCopy(start_points[i], verts[4*i+0].pos);
		VectorCopy(end_points[i], verts[4*i+1].pos);

		pointb = ( i + 1 ) % NUM_BEAM_SEGS;

		VectorCopy(start_points[pointb], verts[4*i+2].pos);
		VectorCopy(end_points[pointb], verts[4*i+3].pos);
	}


	GL3_Add3DdrawCmdToBatch(verts, NUM_BEAM_SEGS*4, GL_TRIANGLE_STRIP, drawCmd);

}

static void
GL3_DrawSpriteModel(entity_t *e, gl3model_t *currentmodel)
{
	float alpha = 1.0F;
	gl3_3D_vtx_t verts[4];
	dsprframe_t *frame;
	float *up, *right;
	dsprite_t *psprite;
	gl3image_t *skin;

	gl3drawCmd_t drawCmd = GL3_CreateDrawCmd();

	/* don't even bother culling, because it's just
	   a single polygon without a surface cache */
	psprite = (dsprite_t *)currentmodel->extradata;

	e->frame %= psprite->numframes;
	frame = &psprite->frames[e->frame];

	/* normal sprite */
	up = vup;
	right = vright;

	if (e->flags & RF_TRANSLUCENT)
	{
		drawCmd.flags |= DCFlag_DisableDepthMask;
		alpha = e->alpha;
	}

	drawCmd.alpha = alpha;

	skin = currentmodel->skins[e->frame];
	if (!skin)
	{
		skin = gl3_notexture; /* fallback... */
	}

	drawCmd.texnum = skin->texnum;

	if (alpha == 1.0)
	{
		// use shader with alpha test
		GL3_SetDrawCmdShader(&drawCmd, &gl3state.si3DspriteAlpha);
	}
	else
	{
		drawCmd.flags |= DCFlag_Blend;

		GL3_SetDrawCmdShader(&drawCmd, &gl3state.si3Dsprite);
	}

	verts[0].texCoord[0] = 0;
	verts[0].texCoord[1] = 1;
	verts[1].texCoord[0] = 0;
	verts[1].texCoord[1] = 0;
	verts[2].texCoord[0] = 1;
	verts[2].texCoord[1] = 0;
	verts[3].texCoord[0] = 1;
	verts[3].texCoord[1] = 1;

	VectorMA( e->origin, -frame->origin_y, up, verts[0].pos );
	VectorMA( verts[0].pos, -frame->origin_x, right, verts[0].pos );

	VectorMA( e->origin, frame->height - frame->origin_y, up, verts[1].pos );
	VectorMA( verts[1].pos, -frame->origin_x, right, verts[1].pos );

	VectorMA( e->origin, frame->height - frame->origin_y, up, verts[2].pos );
	VectorMA( verts[2].pos, frame->width - frame->origin_x, right, verts[2].pos );

	VectorMA( e->origin, -frame->origin_y, up, verts[3].pos );
	VectorMA( verts[3].pos, frame->width - frame->origin_x, right, verts[3].pos );


	GL3_Add3DdrawCmdToBatch(verts, 4, GL_TRIANGLE_FAN, drawCmd);

}

static void
GL3_DrawNullModel(entity_t *currententity)
{
	vec3_t shadelight;
	gl3drawCmd_t drawCmd = GL3_CreateDrawCmd();

	if(currententity->flags & RF_TRANSLUCENT)
		drawCmd.flags |= DCFlag_DisableDepthMask;

	if (currententity->flags & RF_FULLBRIGHT)
	{
		shadelight[0] = shadelight[1] = shadelight[2] = 1.0F;
	}
	else
	{
		GL3_LightPoint(currententity, currententity->origin, shadelight);
	}

	GL3_RotateForEntity(currententity, &drawCmd);

	drawCmd.alpha = 1.0f;
	for(int i=0; i<3; ++i)
	{
		int c = shadelight[i] * 255.0f;
		drawCmd.color[i] = c & 255;
	}
	drawCmd.flags |= DCFlag_UseColor;

	GL3_SetDrawCmdShader(&drawCmd, &gl3state.si3DcolorOnly);

	gl3_3D_vtx_t vtxA[6] = {
		{{0, 0, -16}, {0,0}, {0,0}},
		{{16 * cos( 0 * M_PI / 2 ), 16 * sin( 0 * M_PI / 2 ), 0}, {0,0}, {0,0}},
		{{16 * cos( 1 * M_PI / 2 ), 16 * sin( 1 * M_PI / 2 ), 0}, {0,0}, {0,0}},
		{{16 * cos( 2 * M_PI / 2 ), 16 * sin( 2 * M_PI / 2 ), 0}, {0,0}, {0,0}},
		{{16 * cos( 3 * M_PI / 2 ), 16 * sin( 3 * M_PI / 2 ), 0}, {0,0}, {0,0}},
		{{16 * cos( 4 * M_PI / 2 ), 16 * sin( 4 * M_PI / 2 ), 0}, {0,0}, {0,0}}
	};

	GL3_Add3DdrawCmdToBatch(vtxA, 6, GL_TRIANGLE_FAN, drawCmd);

	gl3_3D_vtx_t vtxB[6] = {
		{{0, 0, 16}, {0,0}, {0,0}},
		vtxA[5], vtxA[4], vtxA[3], vtxA[2], vtxA[1]
	};

	GL3_Add3DdrawCmdToBatch(vtxB, 6, GL_TRIANGLE_FAN, drawCmd);

}

static void
GL3_DrawParticles(void)
{
	// TODO: stereo
	//qboolean stereo_split_tb = ((gl_state.stereo_mode == STEREO_SPLIT_VERTICAL) && gl_state.camera_separation);
	//qboolean stereo_split_lr = ((gl_state.stereo_mode == STEREO_SPLIT_HORIZONTAL) && gl_state.camera_separation);

	//if (!(stereo_split_tb || stereo_split_lr))
	{
		int i;
		int numParticles = r_newrefdef.num_particles;
		YQ2_ALIGNAS_TYPE(unsigned) byte color[4];
		const particle_t *p;
		// assume the size looks good with window height 480px and scale according to real resolution
		float pointSize = gl3_particle_size->value * (float)r_newrefdef.height/480.0f;

		typedef struct part_vtx {
			GLfloat pos[3];
			GLfloat size;
			GLfloat dist;
			GLfloat color[4];
		} part_vtx;
		YQ2_STATIC_ASSERT(sizeof(part_vtx)==9*sizeof(float), "invalid part_vtx size"); // remember to update GL3_SurfInit() if this changes!

		// Don't try to draw particles if there aren't any.
		if (numParticles == 0)
		{
			return;
		}

		YQ2_VLA(part_vtx, buf, numParticles);

		// TODO: viewOrg could be in UBO
		vec3_t viewOrg;
		VectorCopy(r_newrefdef.vieworg, viewOrg);

		glDepthMask(GL_FALSE);
		glEnable(GL_BLEND);

#ifdef YQ2_GL3_GLES
		// the RPi4 GLES3 implementation doesn't draw particles if culling is
		// enabled (at least with GL_FRONT which seems to be default in q2?)
		glDisable(GL_CULL_FACE);
#else
		// GLES doesn't have this, maybe it's always enabled? (https://gamedev.stackexchange.com/a/15528 says it works)
		// luckily we don't use glPointSize() but set gl_PointSize in shader anyway
		glEnable(GL_PROGRAM_POINT_SIZE);
#endif

		GL3_UseProgram(gl3state.siParticle.shaderProgram);

		for ( i = 0, p = r_newrefdef.particles; i < numParticles; i++, p++ )
		{
			*(int *) color = d_8to24table [ p->color & 0xFF ];
			part_vtx* cur = &buf[i];
			vec3_t offset; // between viewOrg and particle position
			VectorSubtract(viewOrg, p->origin, offset);

			VectorCopy(p->origin, cur->pos);
			cur->size = pointSize;
			cur->dist = VectorLength(offset);

			for(int j=0; j<3; ++j)  cur->color[j] = color[j]*(1.0f/255.0f);

			cur->color[3] = p->alpha;
		}

		GL3_BindVAO(gl3state.vaoParticle);
		GL3_BindVBO(gl3state.vboParticle);
		glBufferData(GL_ARRAY_BUFFER, sizeof(part_vtx)*numParticles, buf, GL_STREAM_DRAW);
		glDrawArrays(GL_POINTS, 0, numParticles);
		++gl3_num3Ddraws;
		++gl3_numBufferVtxData;

		glDisable(GL_BLEND);
		glDepthMask(GL_TRUE);
#ifdef YQ2_GL3_GLES
		if(r_cull->value != 0.0f)
			glEnable(GL_CULL_FACE);
#else
		glDisable(GL_PROGRAM_POINT_SIZE);
#endif

		YQ2_VLAFREE(buf);
	}
}

static void
GL3_DrawEntitiesOnList(void)
{
	int i;

	if (!r_drawentities->value)
	{
		return;
	}

	GL3_ResetShadowAliasModels();

	/* draw non-transparent first */
	for (i = 0; i < r_newrefdef.num_entities; i++)
	{
		entity_t *currententity = &r_newrefdef.entities[i];

		if (currententity->flags & RF_TRANSLUCENT)
		{
			continue; /* solid */
		}

		if (currententity->flags & RF_BEAM)
		{
			GL3_DrawBeam(currententity);
		}
		else
		{
			gl3model_t *currentmodel = currententity->model;

			if (!currentmodel)
			{
				GL3_DrawNullModel(currententity);
				continue;
			}

			switch (currentmodel->type)
			{
				case mod_alias:
					GL3_DrawAliasModel(currententity);
					break;
				case mod_brush:
					GL3_DrawBrushModel(currententity, currentmodel);
					break;
				case mod_sprite:
					GL3_DrawSpriteModel(currententity, currentmodel);
					break;
				default:
					Com_Error(ERR_DROP, "Bad modeltype");
					break;
			}
		}
	}

	/* draw transparent entities
	   we could sort these if it ever
	   becomes a problem... */

	// make sure that drawing the solid entities is done first
	GL3_Draw3DBatchesNow();

	glDepthMask(GL_FALSE);

	for (i = 0; i < r_newrefdef.num_entities; i++)
	{
		entity_t *currententity = &r_newrefdef.entities[i];

		if (!(currententity->flags & RF_TRANSLUCENT))
		{
			continue; /* solid */
		}

		if (currententity->flags & RF_BEAM)
		{
			GL3_DrawBeam(currententity);
		}
		else
		{
			gl3model_t *currentmodel = currententity->model;

			if (!currentmodel)
			{
				GL3_DrawNullModel(currententity);
				continue;
			}

			switch (currentmodel->type)
			{
				case mod_alias:
					GL3_DrawAliasModel(currententity);
					break;
				case mod_brush:
					GL3_DrawBrushModel(currententity, currentmodel);
					break;
				case mod_sprite:
					GL3_DrawSpriteModel(currententity, currentmodel);
					break;
				default:
					Com_Error(ERR_DROP, "Bad modeltype");
					break;
			}
		}
	}

	// make sure that drawing the entities is done before drawing the shadows
	GL3_Draw3DBatchesNow();

	glDepthMask(GL_FALSE);

	GL3_DrawAliasShadows();

	glDepthMask(GL_TRUE); /* back to writing */

}

static void
SetupFrame(void)
{
	int i;
	mleaf_t *leaf;

	gl3_framecount++;

	/* build the transformation matrix for the given view angles */
	VectorCopy(r_newrefdef.vieworg, gl3_origin);

	AngleVectors(r_newrefdef.viewangles, vpn, vright, vup);

	/* current viewcluster */
	if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		if (!gl3_worldmodel)
		{
			Com_Error(ERR_DROP, "%s: bad world model", __func__);
			return;
		}

		gl3_oldviewcluster = gl3_viewcluster;
		gl3_oldviewcluster2 = gl3_viewcluster2;
		leaf = Mod_PointInLeaf(gl3_origin, gl3_worldmodel->nodes);
		gl3_viewcluster = gl3_viewcluster2 = leaf->cluster;

		/* check above and below so crossing solid water doesn't draw wrong */
		if (!leaf->contents)
		{
			/* look down a bit */
			vec3_t temp;

			VectorCopy(gl3_origin, temp);
			temp[2] -= 16;
			leaf = Mod_PointInLeaf(temp, gl3_worldmodel->nodes);

			if (!(leaf->contents & CONTENTS_SOLID) &&
				(leaf->cluster != gl3_viewcluster2))
			{
				gl3_viewcluster2 = leaf->cluster;
			}
		}
		else
		{
			/* look up a bit */
			vec3_t temp;

			VectorCopy(gl3_origin, temp);
			temp[2] += 16;
			leaf = Mod_PointInLeaf(temp, gl3_worldmodel->nodes);

			if (!(leaf->contents & CONTENTS_SOLID) &&
				(leaf->cluster != gl3_viewcluster2))
			{
				gl3_viewcluster2 = leaf->cluster;
			}
		}
	}

	for (i = 0; i < 4; i++)
	{
		v_blend[i] = r_newrefdef.blend[i];
	}

	c_brush_polys = 0;
	c_alias_polys = 0;

	/* clear out the portion of the screen that the NOWORLDMODEL defines */
	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
	{
		glEnable(GL_SCISSOR_TEST);
		glClearColor(0.3, 0.3, 0.3, 1);
		glScissor(r_newrefdef.x,
				vid.height - r_newrefdef.height - r_newrefdef.y,
				r_newrefdef.width, r_newrefdef.height);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		GL3_ResetClearColor();
		glDisable(GL_SCISSOR_TEST);
	}
}

static void
GL3_SetGL2D(void)
{
	/* OpenMerc: SPEC-412 §2 / SPEC-413 §6 2D state (whole window, depth range +-9999) */
	GL3_SoF2D_Enter2D(false);
}

// equivalent to R_x * R_y * R_z where R_x is the trans matrix for rotating around X axis for aroundXdeg
static hmm_mat4 rotAroundAxisXYZ(float aroundXdeg, float aroundYdeg, float aroundZdeg)
{
	float alpha = HMM_ToRadians(aroundXdeg);
	float beta = HMM_ToRadians(aroundYdeg);
	float gamma = HMM_ToRadians(aroundZdeg);

	float sinA = HMM_SinF(alpha);
	float cosA = HMM_CosF(alpha);
	float sinB = HMM_SinF(beta);
	float cosB = HMM_CosF(beta);
	float sinG = HMM_SinF(gamma);
	float cosG = HMM_CosF(gamma);

	hmm_mat4 ret = {{
		{  cosB*cosG,  sinA*sinB*cosG + cosA*sinG, -cosA*sinB*cosG + sinA*sinG, 0 }, // first *column*
		{ -cosB*sinG, -sinA*sinB*sinG + cosA*cosG,  cosA*sinB*sinG + sinA*cosG, 0 },
		{  sinB,      -sinA*cosB,                   cosA*cosB,                  0 },
		{  0,          0,                           0,                          1 }
	}};

	return ret;
}

// equivalent to R_SetPerspective() but returning a matrix instead of setting internal OpenGL state
hmm_mat4
GL3_SetPerspective(GLdouble fovy)
{
	/* OpenMerc: SPEC-413 §6 - near = r_nearclipdist x f(fov_y), far = r_farclipdist */
	GLdouble f = 1.0;
	GLdouble zNear, zFar;
	const GLdouble aspect = (GLdouble)r_newrefdef.width / r_newrefdef.height;

	if (fovy < 5)
	{
		f = 3.9;
	}
	else if (fovy < 75)
	{
		f = 1.0 + (75.0 - fovy) / 70.0 * 2.9;
	}

	zNear = Q_max(r_nearclipdist->value, 0.1f) * f;
	zFar = Q_max(r_farclipdist->value, zNear + 1);

	// calculation of left, right, bottom, top is from R_MYgluPerspective() of old gl backend
	// which seems to be slightly different from the real gluPerspective()
	// and thus also from HMM_Perspective()
	GLdouble left, right, bottom, top;
	float A, B, C, D;

	top = zNear * tan(fovy * M_PI / 360.0);
	right = top * aspect;

	bottom = -top;
	left = -right;

	// the following emulates glFrustum(left, right, bottom, top, zNear, zFar)
	// see https://www.khronos.org/registry/OpenGL-Refpages/gl2.1/xhtml/glFrustum.xml
	//  or http://docs.gl/gl2/glFrustum#description (looks better in non-Firefox browsers)
	A = (right+left)/(right-left);
	B = (top+bottom)/(top-bottom);
	C = -(zFar+zNear)/(zFar-zNear);
	D = -(2.0*zFar*zNear)/(zFar-zNear);

	hmm_mat4 ret = {{
		{ (2.0*zNear)/(right-left), 0, 0, 0 }, // first *column*
		{ 0, (2.0*zNear)/(top-bottom), 0, 0 },
		{ A, B, C, -1.0 },
		{ 0, 0, D, 0 }
	}};

	return ret;
}

static void GL3_Clear(void);

static void
SetupGL(void)
{
	int x, x2, y2, y, w, h;

	/* set up viewport */
	x = floor(r_newrefdef.x * vid.width / (float)vid.width);
	x2 = ceil((r_newrefdef.x + r_newrefdef.width) * vid.width / (float)vid.width);
	y = floor(vid.height - r_newrefdef.y * vid.height / (float)vid.height);
	y2 = ceil(vid.height - (r_newrefdef.y + r_newrefdef.height) * vid.height / (float)vid.height);

	w = x2 - x;
	h = y - y2;

#if 0 // TODO: stereo stuff
	qboolean drawing_left_eye = gl_state.camera_separation < 0;
	qboolean stereo_split_tb = ((gl_state.stereo_mode == STEREO_SPLIT_VERTICAL) && gl_state.camera_separation);
	qboolean stereo_split_lr = ((gl_state.stereo_mode == STEREO_SPLIT_HORIZONTAL) && gl_state.camera_separation);

	if(stereo_split_lr) {
		w = w / 2;
		x = drawing_left_eye ? (x / 2) : (x + vid.width) / 2;
	}

	if(stereo_split_tb) {
		h = h / 2;
		y2 = drawing_left_eye ? (y2 + vid.height) / 2 : (y2 / 2);
	}
#endif // 0

	// set up the FBO accordingly, but only if actually rendering the world
	// (=> don't use FBO when rendering the playermodel in the player menu)
	// also, only do this when under water, because this has a noticeable overhead on some systems
	if (gl3_usefbo->value && gl3state.ppFBO != 0
		&& (r_newrefdef.rdflags & (RDF_NOWORLDMODEL|RDF_UNDERWATER)) == RDF_UNDERWATER)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, gl3state.ppFBO);
		gl3state.ppFBObound = true;
		if(gl3state.ppFBtex == 0)
		{
			gl3state.ppFBtexWidth = -1; // make sure we generate the texture storage below
			glGenTextures(1, &gl3state.ppFBtex);
		}

		if(gl3state.ppFBrbo == 0)
		{
			gl3state.ppFBtexWidth = -1; // make sure we generate the RBO storage below
			glGenRenderbuffers(1, &gl3state.ppFBrbo);
		}

		// even if the FBO already has a texture and RBO, the viewport size
		// might have changed so they need to be regenerated with the correct sizes
		if(gl3state.ppFBtexWidth != w || gl3state.ppFBtexHeight != h)
		{
			gl3state.ppFBtexWidth = w;
			gl3state.ppFBtexHeight = h;
			GL3_Bind(gl3state.ppFBtex);
			// create texture for FBO with size of the viewport
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			GL3_Bind(0);
			// attach it to currently bound FBO
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl3state.ppFBtex, 0);

			// also create a renderbuffer object so the FBO has a stencil- and depth-buffer
			glBindRenderbuffer(GL_RENDERBUFFER, gl3state.ppFBrbo);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
			glBindRenderbuffer(GL_RENDERBUFFER, 0);
			// attach it to the FBO
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
			                          GL_RENDERBUFFER, gl3state.ppFBrbo);

			GLenum fbState = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if(fbState != GL_FRAMEBUFFER_COMPLETE)
			{
				Com_Printf("GL3 SetupGL(): WARNING: FBO is not complete, status = 0x%x\n", fbState);
				gl3state.ppFBtexWidth = -1; // to try again next frame; TODO: maybe give up?
				gl3state.ppFBObound = false;
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
			}
		}

		GL3_Clear(); // clear the FBO that's bound now

		glViewport(0, 0, w, h); // this will be moved to the center later, so no x/y offset
	}
	else if (!GL3_Post_BeginView(x, y2, w, h)) // rendering directly (not to FBO for postprocessing)
	{
		glViewport(x, y2, w, h);
	}

	/* set up projection matrix (eye coordinates -> clip coordinates) */
	gl3state.projMat3D = GL3_SetPerspective(r_newrefdef.fov_y);

	glCullFace(GL_FRONT);

	/* set up view matrix (world coordinates -> eye coordinates) */
	{
		// first put Z axis going up
		hmm_mat4 viewMat = {{
			{  0, 0, -1, 0 }, // first *column* (the matrix is column-major)
			{ -1, 0,  0, 0 },
			{  0, 1,  0, 0 },
			{  0, 0,  0, 1 }
		}};

		// now rotate by view angles
		hmm_mat4 rotMat = rotAroundAxisXYZ(-r_newrefdef.viewangles[2], -r_newrefdef.viewangles[0], -r_newrefdef.viewangles[1]);

		viewMat = HMM_MultiplyMat4( viewMat, rotMat );

		// .. and apply translation for current position
		hmm_vec3 trans = HMM_Vec3(-r_newrefdef.vieworg[0], -r_newrefdef.vieworg[1], -r_newrefdef.vieworg[2]);
		viewMat = HMM_MultiplyMat4( viewMat, HMM_Translate(trans) );

		gl3state.viewMat3D = viewMat;
	}

	// just use one projection-view-matrix (premultiplied here)
	// so we have one less mat4 multiplication in the 3D shaders
	gl3state.uni3DData.transProjViewMat4 = HMM_MultiplyMat4(gl3state.projMat3D, gl3state.viewMat3D);

	gl3state.uni3DData.transModelMat4 = gl3_identityMat4;

	gl3state.uni3DData.time = r_newrefdef.time;

	/* GFX-modern: SoF Buddy-style overbright option (non-original, r_om_overbright) scales the lightmaps */
	gl3state.uni3DData.overbrightbits = GL3_Post_Overbright();

	/* GFX-modern: per-pixel SoF dlights this view (r_om_pdlight) */
	GL3_DynLight_SetupView(&gl3state.uni3DData);

	GL3_UpdateUBO3D();

	/* set drawing parms */
	if (r_cull->value)
	{
		glEnable(GL_CULL_FACE);
	}
	else
	{
		glDisable(GL_CULL_FACE);
	}

	glEnable(GL_DEPTH_TEST);
}

extern int c_visible_lightmaps, c_visible_textures;

static void
GL3_RenderView(refdef_t *fd)
{
	if (r_norefresh->value)
	{
		return;
	}

	r_newrefdef = *fd;
	GL3_Fog_ViewTest(&r_newrefdef);
	GL3_DynLight_TestLight(&r_newrefdef); /* GFX-modern: r_om_pdlight_test A/B light */

	if (!gl3_worldmodel && !(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		Com_Error(ERR_DROP, "R_RenderView: NULL worldmodel");
	}

	c_brush_polys = 0;
	c_alias_polys = 0;
	gl3_c_fogpolys = 0; /* I4c: r_speeds 1 "fp" */

	/* OpenMerc: SPEC-410 World part of a frame */
	GL3_SoF_UpdateDetailScroll(gl3_sofframe.active ? gl3_sofframe.gametime_delta : 0.0f);

	GL3_PushDlights();
	GL3_DynLight_BeginFrame(); /* I4c: blocks lit in the last frame back to 0 */

	if (gl_finish->value)
	{
		glFinish();
	}

	SetupFrame();

	R_SetFrustum(vup, vpn, vright, gl3_origin,
		r_newrefdef.fov_x, r_newrefdef.fov_y, frustum);

	/* GFX-modern: the sun shadow maps of this view (r_om_csm) */
	GL3_CSM_Frame();

	SetupGL();

	GL3_MarkLeaves(); /* done here so we know if we're in water */

	GL3_FX_BeginFrame3D();
	GL3_Fog_BeginFrame();

	/* SPEC-410: lightmaps through the colour table; none with r_fullbright or night vision */
	GL3_SoF_BindLightTable((r_fullbright->value != 0) || (r_newrefdef.rdflags & RDF_IRGOGGLES));
	/* I4c: gl_monolightmap texel modes (SPEC-410 Lightmaps) */
	GL3_SoF_ApplyMonoLightmap((r_fullbright->value != 0) || (r_newrefdef.rdflags & RDF_IRGOGGLES));

	GL3_DrawWorld();

	/* GFX-modern: depth of the world for soft particles */
	GL3_Post_DepthCopy();

	/* fog overlay for the faces collected by the world walk, with shimmer and far wall */
	GL3_Fog_DrawPass(true);

	/* GFX-modern: volumetric fog in the fog regions, over the world and its overlay */
	GL3_Post_VolFog();

	if (!gl3_sofframe.active)
	{
		/* Quake II frames: entities and particles as before */
		GL3_DrawEntitiesOnList();
		GL3_DrawParticles();
		GL3_DrawAlphaSurfaces();
		GL3_Draw3DBatchesNow();
	}

	/* SPEC-412 §10: entities, alpha quads, quad chains, translucent world surfaces */
	GL3_FX_SortedPass();

	/* fog overlay for faces collected during the sorted pass */
	GL3_Fog_DrawPass(false);

	// make sure all remaining batched 3D stuff is drawn
	GL3_Draw3DBatchesNow();

	/* GFX-modern: bloom and the copy of the view into the window */
	GL3_Post_EndView();

	if (r_speeds->value == 1)
	{
		Com_Printf("%4i:wp %4i:fp %4i:gp %3i:tx %3i:lm %3i:gt %3i:tp %3i:qp\n",
				c_brush_polys, gl3_c_fogpolys, c_alias_polys, c_visible_textures,
				c_visible_lightmaps, 0, gl3_c_terrainpolys, gl3_sofframe.active ? gl3_c_alphaquads : 0);
	}
}

/* SPEC-413 §6: r_lightlevel := 150 x the largest component of the light at the eye */
static void
GL3_SetLightLevel(entity_t *currententity)
{
	vec3_t shadelight = {0};

	(void)currententity;

	if ((r_newrefdef.rdflags & RDF_NOWORLDMODEL) || !gl3_worldmodel)
	{
		return;
	}

	GL3_SoF_LightPoint(r_newrefdef.vieworg, shadelight, 255);
	r_lightlevel->value = 150 * Q_max(shadelight[0], Q_max(shadelight[1], shadelight[2]));
}

static void
GL3_RenderFrame(refdef_t *fd)
{
	// in case some batched 2D draws are outstanding, draw them now
	GL3_DrawCurrent2Dbatch();
	GL3_SoF2D_Flush();

	GL3_RenderView(fd);

	if (r_norefresh->value)
	{
		r_newrefdef = *fd;
	}

	GL3_SetLightLevel(NULL);
	qboolean usedFBO = gl3state.ppFBObound; // if it was/is used this frame
	if(usedFBO)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0); // now render to default framebuffer
		gl3state.ppFBObound = false;
	}
	GL3_SetGL2D();

	int x = (vid.width - r_newrefdef.width)/2;
	int y = (vid.height - r_newrefdef.height)/2;
	if (usedFBO)
	{
		// if we're actually drawing the world and using an FBO, render the FBO's texture
		GL3_DrawFrameBufferObject(x, y, r_newrefdef.width, r_newrefdef.height, gl3state.ppFBtex, v_blend);
	}

	/* SPEC-412 §11: night vision and the screen tint over the 3D view */
	if (!r_norefresh->value)
	{
		GL3_FX_ScreenOverlays();
	}

	GL3_SoF2D_Enter2D(false);
}

/* OpenMerc: entry for the SPEC-002 RenderFrame (gl3_sofapi.c) */
void
GL3_RenderFrameInternal(refdef_t *fd)
{
	GL3_RenderFrame(fd);
}

static void
GL3_Clear(void)
{
	/* SPEC-413 §6 Clearing (depth writes must be on for the depth clear; slot 0xb4 turned them off) */
	glDepthMask(GL_TRUE);

	if (gl_ztrick->value)
	{
		static int trickframe;

		if (gl_clear->value)
		{
			glClearColor(1, 0, 0.5, 0.5);
			glClear(GL_COLOR_BUFFER_BIT);
		}

		trickframe++;

		if (trickframe & 1)
		{
			gl3depthmin = 0;
			gl3depthmax = 0.49999f;
			glDepthFunc(GL_LEQUAL);
		}
		else
		{
			gl3depthmin = 1;
			gl3depthmax = 0.5f;
			glDepthFunc(GL_GEQUAL);
		}
	}
	else
	{
		if (gl_clear->value)
		{
			glClearColor(0, 0, 0, 0);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			GL3_ResetClearColor();
		}
		else
		{
			glClear(GL_DEPTH_BUFFER_BIT);
		}

		gl3depthmin = 0;
		gl3depthmax = 1;
		glDepthFunc(GL_LEQUAL);
	}

	glDepthRange(gl3depthmin, gl3depthmax);
}

#ifdef OM_SOFREF
extern void GL3_SoF_RequestRestart(void);
#endif

void
GL3_BeginFrame(float camera_separation)
{
	(void)camera_separation;

	GL3_Post_GpuTimer(true); /* r_om_gputime measurement */

	/* SPEC-413 §6 Begin frame */
#ifdef OM_SOFREF
	if (gl_mode->modified || vid_fullscreen->modified)
	{
		GL3_SoF_RequestRestart();
	}
#endif

	if (gl3_particle_square->modified || gl3_colorlight->modified)
	{
		gl3_particle_square->modified = false;
		gl3_colorlight->modified = false;
		GL3_RecreateShaders();
	}

#ifndef OM_SOFREF
	/* the Yamagi host has no cvar callbacks: validate here */
	if (r_isf->modified || r_iaf->modified)
	{
		r_isf->modified = r_iaf->modified = false;
		GL3_SoF_CheckFormatCvar(r_isf, "GL_SOLID_FORMAT");
		GL3_SoF_CheckFormatCvar(r_iaf, "GL_ALPHA_FORMAT");
	}
#endif

	/* draw buffer stuff */
	if (gl_drawbuffer->modified)
	{
		gl_drawbuffer->modified = false;

#ifndef YQ2_GL3_GLES
		glDrawBuffer(Q_stricmp(gl_drawbuffer->string, "GL_FRONT") == 0 ? GL_FRONT : GL_BACK);
#endif
	}

	/* texturemode stuff */
	if (gl_texturemode->modified || gl_ext_anisotropic->modified || gl_anisotropic->modified
	    || gl_nolerp_list->modified || r_lerp_list->modified
		|| r_2D_unfiltered->modified || r_videos_unfiltered->modified)
	{
		GL3_TextureMode(gl_texturemode->string);
		gl_texturemode->modified = false;
		gl_ext_anisotropic->modified = false;
		gl_anisotropic->modified = false;
		gl_nolerp_list->modified = false;
		r_lerp_list->modified = false;
		r_2D_unfiltered->modified = false;
		r_videos_unfiltered->modified = false;
	}

	if (r_vsync->modified)
	{
		r_vsync->modified = false;
		GL3_SetVsync();
	}

	/* clear screen if desired */
	GL3_Clear();

	/* go into 2D mode */
	GL3_SetGL2D();
}

static void
GL3_SetPalette(const unsigned char *palette)
{
	int i;
	byte *rp = (byte *)gl3_rawpalette;

	if (palette)
	{
		for (i = 0; i < 256; i++)
		{
			rp[i * 4 + 0] = palette[i * 3 + 0];
			rp[i * 4 + 1] = palette[i * 3 + 1];
			rp[i * 4 + 2] = palette[i * 3 + 2];
			rp[i * 4 + 3] = 0xff;
		}
	}
	else
	{
		for (i = 0; i < 256; i++)
		{
			rp[i * 4 + 0] = LittleLong(d_8to24table[i]) & 0xff;
			rp[i * 4 + 1] = (LittleLong(d_8to24table[i]) >> 8) & 0xff;
			rp[i * 4 + 2] = (LittleLong(d_8to24table[i]) >> 16) & 0xff;
			rp[i * 4 + 3] = 0xff;
		}
	}

	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	GL3_ResetClearColor();
}

/*
=====================
GL3_EndWorldRenderpass
=====================
*/
static qboolean
GL3_EndWorldRenderpass( void )
{
	return true;
}

/* OpenMerc: SoF module start (gl3_sof.c) */
qboolean
GL3_InitForSoF(void)
{
	return GL3_Init();
}

#ifndef OM_SOFREF
Q2_DLL_EXPORTED refexport_t
GetRefAPI(refimport_t imp)
{
	refexport_t re = {0};

	ri = imp;

	re.api_version = API_VERSION;
	re.framework_version = GL3_GetSDLVersion();

	re.Init = GL3_Init;
	re.Shutdown = GL3_Shutdown;
	re.PrepareForWindow = GL3_PrepareForWindow;
	re.InitContext = GL3_InitContext;
	re.GetDrawableSize = GL3_GetDrawableSize;
	re.ShutdownContext = GL3_ShutdownContext;
	re.IsVSyncActive = GL3_IsVsyncActive;

	re.BeginRegistration = GL3_BeginRegistration;
	re.RegisterModel = GL3_RegisterModel;
	re.RegisterSkin = GL3_RegisterSkin;

	re.SetSky = GL3_SetSky;
	re.EndRegistration = GL3_EndRegistration;

	re.RenderFrame = GL3_RenderFrame;

	re.DrawFindPic = GL3_Draw_FindPic;
	re.DrawGetPicSize = GL3_Draw_GetPicSize;

	re.DrawPicScaled = GL3_Draw_PicScaled;
	re.DrawPicScaledCol = GL3_Draw_PicScaledCol;
	re.DrawStretchPic = GL3_Draw_StretchPic;

	re.DrawCharScaled = GL3_Draw_CharScaled;
	re.DrawTileClear = GL3_Draw_TileClear;
	re.DrawFill = GL3_Draw_Fill;
	re.DrawFadeScreen = GL3_Draw_FadeScreen;

	re.DrawStretchRaw = GL3_Draw_StretchRaw;
	re.SetPalette = GL3_SetPalette;

	re.BeginFrame = GL3_BeginFrame;
	re.EndWorldRenderpass = GL3_EndWorldRenderpass;
	re.EndFrame = GL3_EndFrame;

    // Tell the client that we're unsing the
	// new renderer restart API.
    ri.Vid_RequestRestart(RESTART_NO);

	return re;
}
#endif /* !OM_SOFREF */
