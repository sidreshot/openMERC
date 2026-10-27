/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: cascaded sun shadows (docs/impl/GFX-modern.md,
 * SPEC-AMEND-016 R16.2 and R16.4), r_om_csm.
 *
 * The sun of a map comes from the worldspawn keys the map compiler used
 * (R16.2): its direction from _sun_angle, _sun / _sun_target (a light entity
 * aimed at an info_null) or _sun_vector, straight down for _sun_surface
 * without a direction key, none otherwise. Maps without a sun get no shadows.
 *
 * The lightmaps of 1.07f already hold the shadows the static world casts in
 * the sun, so the world is not shadowed a second time. Two depth maps are
 * kept per cascade: the static world (opaque world faces) and the dynamic
 * casters (Ghoul models, as drawn in the previous frame). A world fragment
 * loses part of its light only where the static world lets the sun through
 * (the lightmap holds sunlight there) and a dynamic caster blocks it; it never
 * gets darker than the map's sun ambient. The Ghoul blob / mask shadows are
 * left out where the ground is in the sun (r_om_csm_replace_ghl), the CSM
 * shadow of the model takes their place; elsewhere (indoors) they stay.
 */

#include "header/local.h"

extern vec3_t vup, vpn, vright; /* gl3_main.c, the view of this frame */

#define CSM_MAXCASC 4
#define CSM_BINDING 4       /* uniform block binding point (after uniLights) */
#define CSM_UNIT 8          /* GL_TEXTURE8 */
#define CSM_MAXDYN (3 * 1024 * 1024)   /* floats of captured caster triangles */

#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif

static cvar_t *r_om_csm, *r_om_csm_cascades, *r_om_csm_size, *r_om_csm_soft, *r_om_csm_bias;
static cvar_t *r_om_csm_normalbias, *r_om_csm_split, *r_om_csm_maxdist, *r_om_csm_replace_ghl;
static cvar_t *r_om_csm_strength, *r_om_csm_debug;

/* the sun of the loaded map */
static struct
{
	qboolean valid;
	vec3_t tosun;      /* unit vector from a lit point toward the sun */
	vec3_t color;
	float light;       /* _sun_light, 100 when missing */
	vec3_t ambient;    /* _sun_ambient / 255 */
	char how[48];
} sun;

/* std140 block uniOMcsm */
typedef struct
{
	float vp[CSM_MAXCASC][16];
	float split[4];    /* far view depth of each cascade */
	float sun[4];      /* toward sun, enabled */
	float params[4];   /* strength, -, normal offset in texels, PCF radius in texels */
	float params2[4];  /* cascades, 1 / size, replace Ghoul shadows, debug view */
	float view[4];     /* view forward, -forward . origin */
	float amb[4];      /* ambient floor (lightmap units) */
	float texel[4];    /* world size of a texel per cascade */
	float bias[4];     /* depth bias per cascade (dynamic layer; static is 4x) */
} csmubo_t;

static struct
{
	qboolean failed, active;
	int size, ncasc;
	GLuint tex, dummy, fbo, prog, vao, vbo, dynvbo, ubo;
	GLint u_vp;
	const gl3model_t *built;
	vec3_t wmins, wmaxs;     /* bounds of the static casters (the world model keeps none of its own) */
	int numstatic;          /* vertices */
	float *dyn;
	int numdyn, maxdyn;     /* floats, 3 per vertex */
	int capframe;
	float invpv[16];
	qboolean invvalid;
	csmubo_t u;
	qboolean uboenabled;
} C;

/* ------------------------------------------------------------------
 * the sun keys (R16.2)
 * ------------------------------------------------------------------ */

typedef struct
{
	char classname[32], targetname[64], target[64];
	vec3_t origin;
	qboolean hasorigin;
} csment_t;

static const char *
Token(const char **p, const char *end, char *out, int outsize)
{
	const char *s = *p;
	int n = 0;

	while ((s < end) && *s && ((unsigned char)*s <= ' '))
	{
		s++;
	}

	if ((s >= end) || !*s)
	{
		*p = s;
		return NULL;
	}

	if ((*s == '{') || (*s == '}'))
	{
		out[0] = *s;
		out[1] = 0;
		*p = s + 1;
		return out;
	}

	if (*s == '"')
	{
		s++;

		while ((s < end) && *s && (*s != '"'))
		{
			if (n < outsize - 1)
			{
				out[n++] = *s;
			}

			s++;
		}

		if ((s < end) && (*s == '"'))
		{
			s++;
		}
	}
	else
	{
		while ((s < end) && *s && ((unsigned char)*s > ' ') && (*s != '{') && (*s != '}'))
		{
			if (n < outsize - 1)
			{
				out[n++] = *s;
			}

			s++;
		}
	}

	out[n] = 0;
	*p = s;
	return out;
}

static int
Floats(const char *s, float *out, int max)
{
	int n = 0;

	while (*s && (n < max))
	{
		char *e;
		float v;

		while (*s && ((unsigned char)*s <= ' '))
		{
			s++;
		}

		if (!*s)
		{
			break;
		}

		v = (float)strtod(s, &e);

		if (e == s)
		{
			break;
		}

		out[n++] = v;
		s = e;
	}

	return n;
}

void
GL3_CSM_LoadSun(const char *ents, int len)
{
	const char *p = ents, *end = ents + len;
	char key[128], val[512];
	csment_t *list = NULL;
	int num = 0, cap = 0, i, j;
	char ws_angle[128] = "", ws_name[64] = "", ws_vector[128] = "", ws_color[128] = "", ws_light[64] = "";
	char ws_ambient[128] = "";
	qboolean ws_surface = false, first = true;

	memset(&sun, 0, sizeof(sun));
	C.built = NULL; /* the static casters of the new map */
	C.numdyn = 0;

	if (!ents || (len <= 0))
	{
		return;
	}

	while (Token(&p, end, key, sizeof(key)))
	{
		csment_t e;

		if (key[0] != '{')
		{
			break;
		}

		memset(&e, 0, sizeof(e));

		while (Token(&p, end, key, sizeof(key)) && (key[0] != '}'))
		{
			if (!Token(&p, end, val, sizeof(val)))
			{
				break;
			}

			if (!Q_stricmp(key, "classname"))
			{
				Q_strlcpy(e.classname, val, sizeof(e.classname));
			}
			else if (!Q_stricmp(key, "targetname"))
			{
				Q_strlcpy(e.targetname, val, sizeof(e.targetname));
			}
			else if (!Q_stricmp(key, "target"))
			{
				Q_strlcpy(e.target, val, sizeof(e.target));
			}
			else if (!Q_stricmp(key, "origin"))
			{
				e.hasorigin = (Floats(val, e.origin, 3) == 3);
			}
			else if (first)
			{
				if (!Q_stricmp(key, "_sun_angle"))
				{
					Q_strlcpy(ws_angle, val, sizeof(ws_angle));
				}
				else if (!Q_stricmp(key, "_sun") || !Q_stricmp(key, "_sun_target"))
				{
					Q_strlcpy(ws_name, val, sizeof(ws_name));
				}
				else if (!Q_stricmp(key, "_sun_vector"))
				{
					Q_strlcpy(ws_vector, val, sizeof(ws_vector));
				}
				else if (!Q_stricmp(key, "_sun_color"))
				{
					Q_strlcpy(ws_color, val, sizeof(ws_color));
				}
				else if (!Q_stricmp(key, "_sun_light") || (!Q_stricmp(key, "_sunlight") && !ws_light[0]))
				{
					Q_strlcpy(ws_light, val, sizeof(ws_light));
				}
				else if (!Q_stricmp(key, "_sun_ambient"))
				{
					Q_strlcpy(ws_ambient, val, sizeof(ws_ambient));
				}
				else if (!Q_stricmp(key, "_sun_surface"))
				{
					ws_surface = true;
				}
			}
		}

		first = false;

		if (num == cap)
		{
			csment_t *n2;

			cap = cap ? cap * 2 : 256;
			n2 = realloc(list, cap * sizeof(*list));

			if (!n2)
			{
				break;
			}

			list = n2;
		}

		list[num++] = e;
	}

	/* direction (the key, not a fit; precedence between kinds is unknown, R16.2) */
	if (ws_angle[0])
	{
		float a[2];

		if (Floats(ws_angle, a, 2) == 2)
		{
			float y = a[0] * (float)M_PI / 180.0f, pt = a[1] * (float)M_PI / 180.0f;

			/* the angles give the direction the light travels */
			sun.tosun[0] = -cosf(y) * cosf(pt);
			sun.tosun[1] = -sinf(y) * cosf(pt);
			sun.tosun[2] = -sinf(pt);
			sun.valid = true;
			Com_sprintf(sun.how, sizeof(sun.how), "_sun_angle %g %g", a[0], a[1]);
		}
	}
	else if (ws_name[0])
	{
		const csment_t *nul = NULL, *light = NULL;

		for (i = 0; i < num; i++)
		{
			if (!Q_stricmp(list[i].targetname, ws_name) && list[i].hasorigin &&
				(!Q_stricmp(list[i].classname, "info_null") || !Q_stricmp(list[i].classname, "info_notnull")))
			{
				nul = &list[i];
				break;
			}
		}

		for (j = 0; nul && (j < num); j++)
		{
			if (!Q_stricmp(list[j].target, ws_name) && list[j].hasorigin &&
				!Q_strncasecmp(list[j].classname, "light", 5))
			{
				light = &list[j];
				break;
			}
		}

		if (nul && light)
		{
			VectorSubtract(light->origin, nul->origin, sun.tosun);
			sun.valid = (VectorNormalize(sun.tosun) > 0.001f);
			Com_sprintf(sun.how, sizeof(sun.how), "_sun %s", ws_name);
		}
	}
	else if (ws_vector[0])
	{
		float v[3];

		if (Floats(ws_vector, v, 3) == 3)
		{
			VectorSet(sun.tosun, -v[0], -v[1], -v[2]);
			sun.valid = (VectorNormalize(sun.tosun) > 0.001f);
			Q_strlcpy(sun.how, "_sun_vector", sizeof(sun.how));
		}
	}
	else if (ws_surface)
	{
		VectorSet(sun.tosun, 0, 0, 1);
		sun.valid = true;
		Q_strlcpy(sun.how, "_sun_surface (down)", sizeof(sun.how));
	}

	/* a sun below the horizon lights nothing from above */
	if (sun.valid && (sun.tosun[2] <= 0.02f))
	{
		sun.valid = false;
	}

	VectorSet(sun.color, 1, 1, 1);

	if (ws_color[0])
	{
		float c[3];

		if (Floats(ws_color, c, 3) == 3)
		{
			VectorSet(sun.color, Q_clamp(c[0], 0, 1), Q_clamp(c[1], 0, 1), Q_clamp(c[2], 0, 1));
		}
	}

	sun.light = 100;

	if (ws_light[0])
	{
		float l;

		if (Floats(ws_light, &l, 1) == 1)
		{
			sun.light = l;
		}
	}

	VectorSet(sun.ambient, 0.06f, 0.06f, 0.06f);

	if (ws_ambient[0])
	{
		float a[3];
		int n = Floats(ws_ambient, a, 3);

		if (n == 3)
		{
			VectorSet(sun.ambient, a[0] / 255.0f, a[1] / 255.0f, a[2] / 255.0f);
		}
		else if (n == 1)
		{
			VectorSet(sun.ambient, a[0] / 255.0f, a[0] / 255.0f, a[0] / 255.0f);
		}
	}

	free(list);

	if (sun.valid)
	{
		R_Printf(PRINT_DEVELOPER, "CSM: sun from %s, toward (%.3f %.3f %.3f), light %g\n", sun.how,
			sun.tosun[0], sun.tosun[1], sun.tosun[2], sun.light);
	}
	else
	{
		R_Printf(PRINT_DEVELOPER, "CSM: no directional sun in this map\n");
	}
}

/* ------------------------------------------------------------------
 * GL objects
 * ------------------------------------------------------------------ */

static const char *csm_vs =
	"in vec3 position;\n"
	"uniform mat4 vp;\n"
	"void main()\n"
	"{\n"
	"	gl_Position = vp * vec4(position, 1.0);\n"
	"}\n";

static const char *csm_fs =
	"out vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"	outColor = vec4(1.0);\n"
	"}\n";

/* the receiving side, put in front of the lightmapped world shaders and the Ghoul emulation */
static const char *csm_glsl =
	"layout (std140) uniform uniOMcsm\n"
	"{\n"
	"	mat4 omCsmVP[4];\n"
	"	vec4 omCsmSplit;\n"
	"	vec4 omCsmSun;\n"
	"	vec4 omCsmParams;\n"
	"	vec4 omCsmParams2;\n"
	"	vec4 omCsmView;\n"
	"	vec4 omCsmAmb;\n"
	"	vec4 omCsmTexel;\n"
	"	vec4 omCsmBias;\n"
	"};\n"
	"uniform highp sampler2DArrayShadow omCsmMap;\n"
	"int omCsmCascade(vec3 wp, out float fade)\n"
	"{\n"
	"	float d = dot(omCsmView.xyz, wp) + omCsmView.w;\n"
	"	int n = int(omCsmParams2.x + 0.5);\n"
	"	fade = 1.0;\n"
	"	for (int i = 0; i < 4; i++)\n"
	"	{\n"
	"		if (i >= n) break;\n"
	"		if (d < omCsmSplit[i])\n"
	"		{\n"
	"			if (i == n - 1) fade = clamp((omCsmSplit[i] - d) / (0.15 * omCsmSplit[i]), 0.0, 1.0);\n"
	"			return i;\n"
	"		}\n"
	"	}\n"
	"	return -1;\n"
	"}\n"
	"vec3 omCsmPos(int c, vec3 wp)\n"
	"{\n"
	"	vec4 p = omCsmVP[c] * vec4(wp, 1.0);\n"
	"	return p.xyz * 0.5 + 0.5;\n"
	"}\n"
	/* 1 where the static world lets the sun through */
	"float omCsmStatic(int c, vec3 wp, vec3 n)\n"
	"{\n"
	"	vec3 p = omCsmPos(c, wp + n * (omCsmTexel[c] * (omCsmParams.z + 1.0)));\n"
	"	if (p.x <= 0.0 || p.y <= 0.0 || p.x >= 1.0 || p.y >= 1.0 || p.z >= 1.0) return 1.0;\n"
	"	return texture(omCsmMap, vec4(p.xy, float(c), p.z - 4.0 * omCsmBias[c]));\n"
	"}\n"
	/* 0..1 of the sun a dynamic caster blocks (percentage-closer filtered) */
	"float omCsmDynamic(int c, vec3 wp, vec3 n)\n"
	"{\n"
	"	vec3 p = omCsmPos(c, wp + n * (omCsmTexel[c] * omCsmParams.z));\n"
	"	if (p.x <= 0.0 || p.y <= 0.0 || p.x >= 1.0 || p.y >= 1.0 || p.z >= 1.0) return 0.0;\n"
	"	float layer = float(c) + omCsmParams2.x;\n"
	"	float zr = p.z - omCsmBias[c];\n"
	"	int r = int(omCsmParams.w + 0.5);\n"
	"	float s = 0.0, w = 0.0;\n"
	"	for (int y = -2; y <= 2; y++)\n"
	"	{\n"
	"		for (int x = -2; x <= 2; x++)\n"
	"		{\n"
	"			if (abs(x) > r || abs(y) > r) continue;\n"
	"			s += texture(omCsmMap, vec4(p.xy + vec2(float(x), float(y)) * omCsmParams2.y, layer, zr));\n"
	"			w += 1.0;\n"
	"		}\n"
	"	}\n"
	"	return 1.0 - s / w;\n"
	"}\n"
	"void omCsmApply(inout vec3 lm, vec3 wp, vec3 n)\n"
	"{\n"
	"	if (omCsmSun.w < 0.5) return;\n"
	"	float ndl = dot(n, omCsmSun.xyz);\n"
	"	float fade;\n"
	"	int c = omCsmCascade(wp, fade);\n"
	/* r_om_csm_debug: red = static sun visibility, green = dynamic shadow, blue = cascade */
	"	if (omCsmParams2.w > 0.5)\n"
	"	{\n"
	"		lm = (c < 0 || ndl <= 0.0) ? vec3(0.0, 0.0, 0.1) : vec3(omCsmStatic(c, wp, n), omCsmDynamic(c, wp, n), float(c + 1) * 0.25);\n"
	"		return;\n"
	"	}\n"
	"	if (ndl <= 0.0 || c < 0) return;\n"
	"	float lit = omCsmStatic(c, wp, n);\n"
	"	if (lit <= 0.01) return;\n"
	"	float sh = omCsmDynamic(c, wp, n);\n"
	"	if (sh <= 0.0) return;\n"
	"	float k = omCsmParams.x * lit * sh * fade * smoothstep(0.0, 0.3, ndl);\n"
	"	lm = max(lm * (1.0 - k), min(lm, omCsmAmb.rgb));\n"
	"}\n"
	/* how much of a Ghoul ground shadow is left: 0 where the ground is in the sun */
	"float omCsmGhoulShadowKeep(vec3 wp)\n"
	"{\n"
	"	if (omCsmSun.w < 0.5 || omCsmParams2.z < 0.5) return 1.0;\n"
	"	float fade;\n"
	"	int c = omCsmCascade(wp, fade);\n"
	"	if (c < 0) return 1.0;\n"
	"	vec3 p = omCsmPos(c, wp + vec3(0.0, 0.0, omCsmTexel[c] * (omCsmParams.z + 1.0)));\n"
	"	if (p.x <= 0.0 || p.y <= 0.0 || p.x >= 1.0 || p.y >= 1.0 || p.z >= 1.0) return 1.0 - fade;\n"
	/* 3 x 3 taps two texels apart: the hand-over to the sun shadow is soft, not texel-blocky */
	"	float s = 0.0;\n"
	"	for (int y = -1; y <= 1; y++)\n"
	"		for (int x = -1; x <= 1; x++)\n"
	"			s += texture(omCsmMap, vec4(p.xy + vec2(float(x), float(y)) * 2.0 * omCsmParams2.y, float(c), p.z - 4.0 * omCsmBias[c]));\n"
	/* a contact shadow stays even in full sun (the sun shadow leaves the feet at a low sun); a weak (night)
	   sun, whose CSM shadow is faint (omCsmParams.x = strength x light / 100), keeps more of it (fb3) */
	"	return 1.0 - (s / 9.0) * fade * 0.7 * clamp(omCsmParams.x / 0.65, 0.0, 1.0);\n"
	"}\n";

void GL3_CSM_Info(void);

const char *
GL3_CSM_Glsl(void)
{
	return csm_glsl;
}

/* malloc'd: the receiver functions followed by src */
char *
GL3_CSM_Prepend(const char *src)
{
	size_t a = strlen(csm_glsl), b = strlen(src);
	char *s = malloc(a + b + 1);

	if (s)
	{
		memcpy(s, csm_glsl, a);
		memcpy(s + a, src, b + 1);
	}

	return s;
}

/* with prog in use: the block binding and the sampler unit */
void
GL3_CSM_BindProgram(GLuint prog)
{
	GLuint bi = glGetUniformBlockIndex(prog, "uniOMcsm");
	GLint loc = glGetUniformLocation(prog, "omCsmMap");

	if (bi != GL_INVALID_INDEX)
	{
		glUniformBlockBinding(prog, bi, CSM_BINDING);
	}

	if (loc != -1)
	{
		glUniform1i(loc, CSM_UNIT);
	}
}

static GLuint
CompileCsm(GLenum type, const char *src)
{
#ifdef YQ2_GL3_GLES3
	const char *version = "#version 300 es\nprecision highp float;\n";
#else
	const char *version = "#version 150\n";
#endif
	const char *srcs[2] = {version, src};
	GLuint s = glCreateShader(type);
	GLint ok = 0;

	glShaderSource(s, 2, srcs, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);

	if (!ok)
	{
		char log[2048];

		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		R_Printf(PRINT_ALL, "GL3 CSM: shader compile failed: %s\n", log);
		glDeleteShader(s);
		return 0;
	}

	return s;
}

static void
UploadUBO(void)
{
	/* the binding cache of gl3_shaders.c (updateUBO) */
	gl3state.currentUBO = C.ubo;
	glBindBuffer(GL_UNIFORM_BUFFER, C.ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(C.u), &C.u, GL_DYNAMIC_DRAW);
}

static void
DepthArray(GLuint tex, int size, int layers)
{
	glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, size, size, layers, 0, GL_DEPTH_COMPONENT,
		GL_UNSIGNED_INT, NULL);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
}

static void
BindMap(void)
{
	GL3_SelectTMU(GL_TEXTURE0 + CSM_UNIT);
	glBindTexture(GL_TEXTURE_2D_ARRAY, C.tex ? C.tex : C.dummy);
	GL3_SelectTMU(GL_TEXTURE0);
}

void
GL3_CSM_Init(void)
{
	r_om_csm = ri.Cvar_Get("r_om_csm", "1", CVAR_ARCHIVE);
	r_om_csm_cascades = ri.Cvar_Get("r_om_csm_cascades", "4", CVAR_ARCHIVE);
	r_om_csm_size = ri.Cvar_Get("r_om_csm_size", "2048", CVAR_ARCHIVE);
	r_om_csm_soft = ri.Cvar_Get("r_om_csm_soft", "2", CVAR_ARCHIVE);
	r_om_csm_bias = ri.Cvar_Get("r_om_csm_bias", "0.0015", CVAR_ARCHIVE);
	r_om_csm_normalbias = ri.Cvar_Get("r_om_csm_normalbias", "0.02", CVAR_ARCHIVE);
	r_om_csm_split = ri.Cvar_Get("r_om_csm_split", "0.75", CVAR_ARCHIVE);
	r_om_csm_maxdist = ri.Cvar_Get("r_om_csm_maxdist", "2048", CVAR_ARCHIVE);
	r_om_csm_replace_ghl = ri.Cvar_Get("r_om_csm_replace_ghl", "1", CVAR_ARCHIVE);
	r_om_csm_strength = ri.Cvar_Get("r_om_csm_strength", "0.65", CVAR_ARCHIVE);
	r_om_csm_debug = ri.Cvar_Get("r_om_csm_debug", "0", 0);

	memset(&C.u, 0, sizeof(C.u));
	C.failed = C.active = false;
	C.tex = C.fbo = C.prog = C.vao = C.vbo = C.dynvbo = 0;
	C.built = NULL;
	C.size = C.ncasc = 0;

	glGenBuffers(1, &C.ubo);
	UploadUBO();
	glBindBufferBase(GL_UNIFORM_BUFFER, CSM_BINDING, C.ubo);
	gl3state.currentUBO = C.ubo;
	C.uboenabled = false;
	ri.Cmd_AddCommand("r_om_csm_info", GL3_CSM_Info);
	ri.Cmd_AddCommand("r_om_fog_regions", GL3_Fog_Regions_f);

	glGenTextures(1, &C.dummy);
	GL3_SelectTMU(GL_TEXTURE0 + CSM_UNIT);
	DepthArray(C.dummy, 1, 2 * CSM_MAXCASC);
	GL3_SelectTMU(GL_TEXTURE0);
	BindMap();
}

void
GL3_CSM_Shutdown(void)
{
	if (C.tex) glDeleteTextures(1, &C.tex);
	if (C.dummy) glDeleteTextures(1, &C.dummy);
	if (C.fbo) glDeleteFramebuffers(1, &C.fbo);
	if (C.prog) glDeleteProgram(C.prog);
	if (C.vao) glDeleteVertexArrays(1, &C.vao);
	if (C.vbo) glDeleteBuffers(1, &C.vbo);
	if (C.dynvbo) glDeleteBuffers(1, &C.dynvbo);
	if (C.ubo) glDeleteBuffers(1, &C.ubo);
	free(C.dyn);
	memset(&C, 0, sizeof(C));
	ri.Cmd_RemoveCommand("r_om_csm_info");
	ri.Cmd_RemoveCommand("r_om_fog_regions");
}

static qboolean
CreateObjects(void)
{
	GLuint vs, fs;
	GLint ok = 0;

	if (C.prog)
	{
		return true;
	}

	vs = CompileCsm(GL_VERTEX_SHADER, csm_vs);
	fs = CompileCsm(GL_FRAGMENT_SHADER, csm_fs);

	if (!vs || !fs)
	{
		return false;
	}

	C.prog = glCreateProgram();
	glAttachShader(C.prog, vs);
	glAttachShader(C.prog, fs);
	glBindAttribLocation(C.prog, GL3_ATTRIB_POSITION, "position");
#ifndef YQ2_GL3_GLES3
	glBindFragDataLocation(C.prog, 0, "outColor");
#endif
	glLinkProgram(C.prog);
	glDeleteShader(vs);
	glDeleteShader(fs);
	glGetProgramiv(C.prog, GL_LINK_STATUS, &ok);

	if (!ok)
	{
		glDeleteProgram(C.prog);
		C.prog = 0;
		return false;
	}

	C.u_vp = glGetUniformLocation(C.prog, "vp");

	glGenVertexArrays(1, &C.vao);
	glGenBuffers(1, &C.vbo);
	glGenBuffers(1, &C.dynvbo);
	glGenFramebuffers(1, &C.fbo);
	return true;
}

static qboolean
CreateMap(int size, int ncasc)
{
	GLenum st;

	if (C.tex && (C.size == size) && (C.ncasc == ncasc))
	{
		return true;
	}

	if (!C.tex)
	{
		glGenTextures(1, &C.tex);
	}

	GL3_SelectTMU(GL_TEXTURE0 + CSM_UNIT);
	DepthArray(C.tex, size, 2 * ncasc);
	GL3_SelectTMU(GL_TEXTURE0);

	glBindFramebuffer(GL_FRAMEBUFFER, C.fbo);
	glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, C.tex, 0, 0);
	{
		GLenum none = GL_NONE;

		glDrawBuffers(1, &none);
		glReadBuffer(GL_NONE);
	}
	st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (st != GL_FRAMEBUFFER_COMPLETE)
	{
		R_Printf(PRINT_ALL, "GL3 CSM: shadow framebuffer incomplete (0x%x), shadows off\n", st);
		glDeleteTextures(1, &C.tex);
		C.tex = 0;
		return false;
	}

	C.size = size;
	C.ncasc = ncasc;
	return true;
}

/* the opaque faces of the world model (not brush entities) as one triangle list */
static void
BuildStatic(void)
{
	const gl3model_t *w = gl3_worldmodel;
	const gl3model_t *sub = (w->numsubmodels > 0) ? &w->submodels[0] : w;
	int first = sub->firstmodelsurface, count = sub->nummodelsurfaces, i, n = 0, cap = 0;
	float *v = NULL;

	if ((first < 0) || (first + count > w->numsurfaces))
	{
		first = 0;
		count = w->numsurfaces;
	}

	for (i = first; i < first + count; i++)
	{
		const msurface_t *s = &w->surfaces[i];
		const glpoly_t *p;
		int tf = s->texinfo ? s->texinfo->flags : 0;

		if (tf & (SURF_SKY | SURF_WARP | SURF_TRANS33 | SURF_TRANS66 | SURF_NODRAW | SOFSURF_ALPHA |
			SOFSURF_FOGBOUNDARY))
		{
			continue;
		}

		if (s->flags & (SURF_DRAWSKY | SURF_DRAWTURB))
		{
			continue;
		}

		for (p = s->polys; p; p = p->next)
		{
			int k;

			if (p->numverts < 3)
			{
				continue;
			}

			if (n + (p->numverts - 2) * 9 > cap)
			{
				float *v2;

				cap = (cap ? cap * 2 : 65536) + (p->numverts - 2) * 9;
				v2 = realloc(v, cap * sizeof(float));

				if (!v2)
				{
					free(v);
					C.numstatic = 0;
					return;
				}

				v = v2;
			}

			for (k = 1; k + 1 < p->numverts; k++)
			{
				memcpy(v + n, p->vertices[0].pos, 3 * sizeof(float));
				memcpy(v + n + 3, p->vertices[k].pos, 3 * sizeof(float));
				memcpy(v + n + 6, p->vertices[k + 1].pos, 3 * sizeof(float));
				n += 9;
			}
		}
	}

	VectorSet(C.wmins, 1e30f, 1e30f, 1e30f);
	VectorSet(C.wmaxs, -1e30f, -1e30f, -1e30f);

	for (i = 0; i < n; i += 3)
	{
		int k;

		for (k = 0; k < 3; k++)
		{
			C.wmins[k] = Q_min(C.wmins[k], v[i + k]);
			C.wmaxs[k] = Q_max(C.wmaxs[k], v[i + k]);
		}
	}

	if (!n)
	{
		VectorClear(C.wmins);
		VectorClear(C.wmaxs);
	}

	GL3_BindVAO(C.vao);
	GL3_BindVBO(C.vbo);
	glBufferData(GL_ARRAY_BUFFER, n * sizeof(float), v, GL_STATIC_DRAW);
	glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
	qglVertexAttribPointer(GL3_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);
	GL3_BindVAO(0);
	free(v);

	C.numstatic = n / 3;
	C.built = w;
	R_Printf(PRINT_DEVELOPER, "CSM: %d static caster triangles\n", C.numstatic / 3);
}

/* ------------------------------------------------------------------
 * per frame
 * ------------------------------------------------------------------ */

static void
OrthoForCascade(int c, float dnear, float dfar, const vec3_t X, const vec3_t Y, const vec3_t Z, float zworld)
{
	float tx = tanf(r_newrefdef.fov_x * (float)M_PI / 360.0f), ty = tanf(r_newrefdef.fov_y * (float)M_PI / 360.0f);
	vec3_t corners[8], center = {0, 0, 0};
	float R = 0, texel, cx, cy, zmax, zmin, k, *m = C.u.vp[c];
	int i;

	for (i = 0; i < 8; i++)
	{
		float d = (i & 4) ? dfar : dnear;
		float sx = (i & 1) ? 1.0f : -1.0f, sy = (i & 2) ? 1.0f : -1.0f;
		int j;

		for (j = 0; j < 3; j++)
		{
			corners[i][j] = r_newrefdef.vieworg[j] + vpn[j] * d + vright[j] * (sx * d * tx) + vup[j] * (sy * d * ty);
		}

		VectorAdd(center, corners[i], center);
	}

	VectorScale(center, 1.0f / 8, center);

	for (i = 0; i < 8; i++)
	{
		vec3_t d;

		VectorSubtract(corners[i], center, d);
		R = Q_max(R, VectorLength(d));
	}

	/* a radius that does not change with the view direction, and centres on whole texels: no shimmer */
	R = ceilf(R / 16.0f) * 16.0f;
	texel = 2 * R / C.size;
	cx = floorf(DotProduct(center, X) / texel) * texel;
	cy = floorf(DotProduct(center, Y) / texel) * texel;
	zmax = zworld + 32;                      /* the highest point of the world toward the sun */
	zmin = DotProduct(center, Z) - R - 32;

	if (zmax < zmin + 64)
	{
		zmax = zmin + 64;
	}

	k = -2.0f / (zmax - zmin);
	memset(m, 0, 16 * sizeof(float));
	m[0] = X[0] / R;
	m[4] = X[1] / R;
	m[8] = X[2] / R;
	m[12] = -cx / R;
	m[1] = Y[0] / R;
	m[5] = Y[1] / R;
	m[9] = Y[2] / R;
	m[13] = -cy / R;
	m[2] = Z[0] * k;
	m[6] = Z[1] * k;
	m[10] = Z[2] * k;
	m[14] = 2.0f * zmax / (zmax - zmin) - 1.0f;
	m[15] = 1;

	C.u.split[c] = dfar;
	C.u.texel[c] = texel;
	/* r_om_csm_bias is per 1000 world units: 0.0015 = 1.5 units */
	C.u.bias[c] = Q_max(r_om_csm_bias->value, 0.0f) * 1000.0f / (zmax - zmin) * 0.5f;
}

static qboolean
WantCsm(void)
{
	if (!r_om_csm || !r_om_csm->value || !sun.valid || C.failed || !gl3_worldmodel)
	{
		return false;
	}

	if (r_newrefdef.rdflags & (RDF_NOWORLDMODEL | SOF_RDF_IRGOGGLES))
	{
		return false;
	}

	if (r_fullbright->value)
	{
		return false;
	}

	return true;
}

qboolean
GL3_CSM_Active(void)
{
	return C.active;
}

/* before SetupGL of a world view: the cascades and both depth maps */
void
GL3_CSM_Frame(void)
{
	int size, ncasc, c, i;
	float lambda, maxd, dnear = 4.0f, prev, zworld = -1e30f;
	vec3_t X, Y, Z, ref;
	GLint oldfunc;
	GLfloat oldrange[2];
	GLboolean wasblend, wascull, wasoffset;

	C.active = false;
	C.invvalid = false;

	if (!WantCsm())
	{
		C.numdyn = 0;

		if (C.uboenabled)
		{
			C.u.sun[3] = 0;
			UploadUBO();
			C.uboenabled = false;
		}

		return;
	}

	if (!CreateObjects())
	{
		C.failed = true;
		return;
	}

	size = (int)r_om_csm_size->value;
	size = (size >= 4096) ? 4096 : (size >= 2048) ? 2048 : (size >= 1024) ? 1024 : 512;
	ncasc = (int)r_om_csm_cascades->value;
	ncasc = (ncasc < 1) ? 1 : (ncasc > CSM_MAXCASC) ? CSM_MAXCASC : ncasc;

	if (!CreateMap(size, ncasc))
	{
		C.failed = true;
		return;
	}

	if (C.built != gl3_worldmodel)
	{
		BuildStatic();
	}

	/* light space: Z toward the sun */
	VectorCopy(sun.tosun, Z);
	VectorSet(ref, 0, 0, 1);

	if (fabsf(Z[2]) > 0.99f)
	{
		VectorSet(ref, 1, 0, 0);
	}

	CrossProduct(ref, Z, X);
	VectorNormalize(X);
	CrossProduct(Z, X, Y);

	for (i = 0; i < 8; i++)
	{
		vec3_t p;

		p[0] = (i & 1) ? C.wmaxs[0] : C.wmins[0];
		p[1] = (i & 2) ? C.wmaxs[1] : C.wmins[1];
		p[2] = (i & 4) ? C.wmaxs[2] : C.wmins[2];
		zworld = Q_max(zworld, DotProduct(p, Z));
	}

	lambda = Q_clamp(r_om_csm_split->value, 0.0f, 1.0f);
	maxd = Q_clamp(r_om_csm_maxdist->value, 256.0f, 16384.0f);
	prev = dnear;

	for (c = 0; c < ncasc; c++)
	{
		float f = (float)(c + 1) / ncasc;
		float uni = dnear + (maxd - dnear) * f, lg = dnear * powf(maxd / dnear, f);
		float d = uni + (lg - uni) * lambda;

		OrthoForCascade(c, (c == 0) ? 0.0f : prev, d, X, Y, Z, zworld);
		prev = d;
	}

	/* the uniform block */
	C.u.sun[0] = Z[0];
	C.u.sun[1] = Z[1];
	C.u.sun[2] = Z[2];
	C.u.sun[3] = 1;
	/* a weak (night) sun casts lighter shadows: _sun_light 100 and more is full strength */
	C.u.params[0] = Q_clamp(r_om_csm_strength->value, 0.0f, 1.0f) * Q_clamp(sun.light / 100.0f, 0.25f, 1.0f);
	C.u.params[2] = Q_max(r_om_csm_normalbias->value, 0.0f) * 100.0f; /* 0.02 = 2 texels */
	C.u.params[3] = Q_clamp(r_om_csm_soft->value, 0.0f, 2.0f);
	C.u.params2[0] = (float)ncasc;
	C.u.params2[1] = 1.0f / size;
	C.u.params2[2] = r_om_csm_replace_ghl->value ? 1.0f : 0.0f;
	C.u.params2[3] = r_om_csm_debug->value ? 1.0f : 0.0f;
	C.u.view[0] = vpn[0];
	C.u.view[1] = vpn[1];
	C.u.view[2] = vpn[2];
	C.u.view[3] = -DotProduct(vpn, r_newrefdef.vieworg);
	C.u.amb[0] = sun.ambient[0];
	C.u.amb[1] = sun.ambient[1];
	C.u.amb[2] = sun.ambient[2];
	UploadUBO();
	C.uboenabled = true;

	/* the depth maps */
	glGetIntegerv(GL_DEPTH_FUNC, &oldfunc);
	glGetFloatv(GL_DEPTH_RANGE, oldrange);
	wasblend = glIsEnabled(GL_BLEND);
	wascull = glIsEnabled(GL_CULL_FACE);
	wasoffset = glIsEnabled(GL_POLYGON_OFFSET_FILL);

	glBindFramebuffer(GL_FRAMEBUFFER, C.fbo);
	glViewport(0, 0, size, size);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDepthRange(0, 1);
	glDepthMask(GL_TRUE);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(1.5f, 2.0f);

	GL3_UseProgram(C.prog);

	if (C.numdyn > 0)
	{
		GL3_BindVAO(C.vao);
		GL3_BindVBO(C.dynvbo);
		glBufferData(GL_ARRAY_BUFFER, C.numdyn * sizeof(float), C.dyn, GL_STREAM_DRAW);
	}

	for (c = 0; c < ncasc; c++)
	{
		static const float one = 1.0f;

		glUniformMatrix4fv(C.u_vp, 1, GL_FALSE, C.u.vp[c]);

		/* static world */
		glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, C.tex, 0, c);
		glClearBufferfv(GL_DEPTH, 0, &one);

		if (C.numstatic)
		{
			GL3_BindVAO(C.vao);
			GL3_BindVBO(C.vbo);
			qglVertexAttribPointer(GL3_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);
			glDrawArrays(GL_TRIANGLES, 0, C.numstatic);
		}

		/* dynamic casters */
		glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, C.tex, 0, ncasc + c);
		glClearBufferfv(GL_DEPTH, 0, &one);

		if (C.numdyn > 0)
		{
			GL3_BindVAO(C.vao);
			GL3_BindVBO(C.dynvbo);
			qglVertexAttribPointer(GL3_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);
			glDrawArrays(GL_TRIANGLES, 0, C.numdyn / 3);
		}
	}

	GL3_BindVAO(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDepthFunc(oldfunc);
	glDepthRange(oldrange[0], oldrange[1]);
	glPolygonOffset(0, 0);

	if (!wasoffset) glDisable(GL_POLYGON_OFFSET_FILL);
	if (wasblend) glEnable(GL_BLEND);
	if (wascull) glEnable(GL_CULL_FACE);

	BindMap();
	C.numdyn = 0;
	C.active = true;
}

/* general 4x4 inverse (column-major), false when singular */
static qboolean
Invert4(const float *m, float *out)
{
	float inv[16], det;
	int i;

	inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

	det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

	if (fabsf(det) < 1e-20f)
	{
		return false;
	}

	det = 1.0f / det;

	for (i = 0; i < 16; i++)
	{
		out[i] = inv[i] * det;
	}

	return true;
}

/* the inverse of this frame's projection * view (after SetupGL) */
const float *
GL3_CSM_InvPV(void)
{
	if (!C.invvalid)
	{
		if (!Invert4(&gl3state.uni3DData.transProjViewMat4.Elements[0][0], C.invpv))
		{
			memset(C.invpv, 0, sizeof(C.invpv));
		}

		C.invvalid = true;
	}

	return C.invpv;
}

/* Ghoul triangles as drawn (clip space, stride in floats): kept in world space for the next frame's
   dynamic depth maps */
void
GL3_CSM_AddCasterTris(const float *clip, int numverts, int stride)
{
	const float *m;
	int i;

	if (!C.active || (numverts < 3))
	{
		return;
	}

	numverts -= numverts % 3;

	if (C.numdyn + numverts * 3 > C.maxdyn)
	{
		int want = C.numdyn + numverts * 3, cap = C.maxdyn ? C.maxdyn : 65536;
		float *d;

		while (cap < want)
		{
			cap *= 2;
		}

		if (cap > CSM_MAXDYN)
		{
			return;
		}

		d = realloc(C.dyn, cap * sizeof(float));

		if (!d)
		{
			return;
		}

		C.dyn = d;
		C.maxdyn = cap;
	}

	m = GL3_CSM_InvPV();

	for (i = 0; i < numverts; i++, clip += stride)
	{
		float x = clip[0], y = clip[1], z = clip[2], w = clip[3];
		float ox = m[0] * x + m[4] * y + m[8] * z + m[12] * w;
		float oy = m[1] * x + m[5] * y + m[9] * z + m[13] * w;
		float oz = m[2] * x + m[6] * y + m[10] * z + m[14] * w;
		float ow = m[3] * x + m[7] * y + m[11] * z + m[15] * w;
		float *o = C.dyn + C.numdyn;

		if (fabsf(ow) < 1e-8f)
		{
			ow = 1e-8f;
		}

		o[0] = ox / ow;
		o[1] = oy / ow;
		o[2] = oz / ow;
		C.numdyn += 3;
	}
}

qboolean
GL3_CSM_ReplaceGhoul(void)
{
	return C.active && r_om_csm_replace_ghl && r_om_csm_replace_ghl->value;
}

/* r_om_csm_info: what the loaded map gives */
void
GL3_CSM_Info(void)
{
	if (!sun.valid)
	{
		R_Printf(PRINT_ALL, "CSM: no directional sun in this map\n");
		return;
	}

	R_Printf(PRINT_ALL, "CSM: sun from %s, toward (%.3f %.3f %.3f), colour (%.2f %.2f %.2f), light %g, "
		"ambient (%.3f %.3f %.3f), %s, %d static / %d dynamic caster triangles\n", sun.how,
		sun.tosun[0], sun.tosun[1], sun.tosun[2], sun.color[0], sun.color[1], sun.color[2], sun.light,
		sun.ambient[0], sun.ambient[1], sun.ambient[2], C.active ? "active" : "inactive",
		C.numstatic / 3, C.numdyn / 9);

	if (C.numdyn >= 3)
	{
		vec3_t mins = {1e30f, 1e30f, 1e30f}, maxs = {-1e30f, -1e30f, -1e30f};
		int i, k;

		for (i = 0; i < C.numdyn; i += 3)
		{
			for (k = 0; k < 3; k++)
			{
				mins[k] = Q_min(mins[k], C.dyn[i + k]);
				maxs[k] = Q_max(maxs[k], C.dyn[i + k]);
			}
		}

		R_Printf(PRINT_ALL, "CSM: dynamic casters within (%.0f %.0f %.0f) - (%.0f %.0f %.0f), view at (%.0f %.0f %.0f)\n",
			mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2],
			r_newrefdef.vieworg[0], r_newrefdef.vieworg[1], r_newrefdef.vieworg[2]);
	}

	/* how much of each depth layer the last frame covered */
	if (C.tex && C.fbo && C.size && (ri.Cmd_Argc() > 1))
	{
		float *px = malloc((size_t)C.size * C.size * sizeof(float));
		int l, i;

		if (!px)
		{
			return;
		}

		glBindFramebuffer(GL_FRAMEBUFFER, C.fbo);

		for (l = 0; l < 2 * C.ncasc; l++)
		{
			int covered = 0;

			glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, C.tex, 0, l);
			glReadPixels(0, 0, C.size, C.size, GL_DEPTH_COMPONENT, GL_FLOAT, px);

			for (i = 0; i < C.size * C.size; i++)
			{
				covered += (px[i] < 1.0f);
			}

			R_Printf(PRINT_ALL, "CSM: layer %d (%s cascade %d): %.2f%% covered\n", l, (l < C.ncasc) ? "static" : "dynamic",
				l % C.ncasc, 100.0f * covered / ((float)C.size * C.size));

			/* the view origin in this layer: its own depth against the stored one, and the stored ones above it */
			{
				const float *m = C.u.vp[l % C.ncasc];
				const float *o = r_newrefdef.vieworg;
				float u = (m[0] * o[0] + m[4] * o[1] + m[8] * o[2] + m[12]) * 0.5f + 0.5f;
				float v = (m[1] * o[0] + m[5] * o[1] + m[9] * o[2] + m[13]) * 0.5f + 0.5f;
				float z = (m[2] * o[0] + m[6] * o[1] + m[10] * o[2] + m[14]) * 0.5f + 0.5f;
				int x = (int)(u * C.size), y = (int)(v * C.size);

				if ((x >= 0) && (y >= 0) && (x < C.size) && (y < C.size))
				{
					R_Printf(PRINT_ALL, "CSM:   view origin at texel %d %d depth %.5f, stored %.5f\n", x, y, z,
						px[y * C.size + x]);
				}
			}
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		free(px);
	}
}
