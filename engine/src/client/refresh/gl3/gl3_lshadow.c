/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: shadows of moving casters from the map's lamps (docs/impl/GFX-modern.md), r_om_lshadow.
 *
 * The lightmaps already hold the shadows the static world casts; what they cannot hold are the shadows of
 * characters and props. The lamps are the map's own: the point lights and spotlights of BSP lump 20, matched to
 * their light entities for the cone and the light style (a lamp switched off casts nothing), and the surface-light
 * patches merged into one emitter per panel. Each frame the few lamps with a moving caster in reach, nearest the
 * eye first, get a cube of depth maps (six faces): the static world (made once when the lamp gets its slot) and the
 * Ghoul models as drawn in the previous frame (gl3_csm.c keeps them). A world fragment the lamp lights in the
 * lightmap (the static faces let it through) and a caster blocks loses that lamp's share of its light: the lamp's
 * light at the point after the compiler's own falloff (linear for lamps, 1/d for panels) against the lightmap.
 */

#include "header/local.h"

#define LS_MAXSLOTS 8
#define LS_BINDING 5        /* uniform block binding point (after uniOMcsm) */
#define LS_UNIT 16          /* GL_TEXTURE16 */
#define LS_TAN 1.08f        /* tan of half the face field of view: a little over 90 degrees for the filter taps */
#define LS_NEAR 2.0f

#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif

static cvar_t *r_om_lshadow, *r_om_lshadow_lights, *r_om_lshadow_size, *r_om_lshadow_strength;
static cvar_t *r_om_lshadow_debug, *r_om_lshadow_range, *r_om_lshadow_gain;

enum { LS_POINT, LS_SPOT, LS_SURF };

typedef struct
{
	vec3_t o, color;
	float intensity, fade, radius;
	int kind, style;
	vec3_t dir;          /* spotlight axis */
	float stopdot, focus;
	vec3_t n;            /* panel normal */
} lslight_t;

/* std140 block uniOMls */
typedef struct
{
	float vp[LS_MAXSLOTS * 6][16];
	float pos[LS_MAXSLOTS][4];   /* origin, radius (0 = empty slot) */
	float col[LS_MAXSLOTS][4];   /* colour x style / 255, kind */
	float a[LS_MAXSLOTS][4];     /* lamp: intensity, fade, focus, cone cosine; panel: normal, intensity x k */
	float dir[LS_MAXSLOTS][4];   /* spotlight axis, nearest caster distance */
	float cone[LS_MAXSLOTS][4];  /* the cone from the lamp around its casters: axis, cosine (-1 all around) */
	float params[4];             /* slots, strength, darkest fraction, texel size per unit of distance */
	float params2[4];            /* 1 / size, debug view, -, - */
} lsubo_t;

static struct
{
	qboolean failed, active, capturing;
	int size, nslots;
	GLuint tex, dummy, fbo, ubo, prog, vao, vbo;
	GLint u_vp;
	int slot[LS_MAXSLOTS];          /* light index, -1 free */
	qboolean dirty[LS_MAXSLOTS];    /* the static faces need drawing */
	const gl3model_t *built;
	lsubo_t u;
	qboolean uboenabled;
	int maxunits;
	int nframes, nstatic, ndynverts;   /* r_om_lshadow_info: calls, static cubes drawn, caster vertices */
} L;

static lslight_t *lights;
static int numlights;

/* ------------------------------------------------------------------
 * the lamps of a map
 * ------------------------------------------------------------------ */

typedef struct
{
	char classname[32], targetname[64], target[64];
	vec3_t origin, mangle;
	qboolean hasorigin, hasmangle, hasangle;
	float angle, cone, focus, fade;
	int style;
} lsent_t;

static lsent_t *
ParseEntities(const char *ents, int len, int *count)
{
	const char *p = ents, *end = ents + len;
	char key[128], val[512];
	lsent_t *list = NULL;
	int num = 0, cap = 0;

	*count = 0;

	if (!ents || (len <= 0))
	{
		return NULL;
	}

	while (GL3_CSM_Token(&p, end, key, sizeof(key)))
	{
		lsent_t e;

		if (key[0] != '{')
		{
			break;
		}

		memset(&e, 0, sizeof(e));
		e.fade = 1;

		while (GL3_CSM_Token(&p, end, key, sizeof(key)) && (key[0] != '}'))
		{
			if (!GL3_CSM_Token(&p, end, val, sizeof(val)))
			{
				break;
			}

			if (!Q_stricmp(key, "classname")) Q_strlcpy(e.classname, val, sizeof(e.classname));
			else if (!Q_stricmp(key, "targetname")) Q_strlcpy(e.targetname, val, sizeof(e.targetname));
			else if (!Q_stricmp(key, "target")) Q_strlcpy(e.target, val, sizeof(e.target));
			else if (!Q_stricmp(key, "origin")) e.hasorigin = (GL3_CSM_Floats(val, e.origin, 3) == 3);
			else if (!Q_stricmp(key, "_mangle")) e.hasmangle = (GL3_CSM_Floats(val, e.mangle, 2) == 2);
			else if (!Q_stricmp(key, "angle")) { e.hasangle = true; e.angle = (float)atof(val); }
			else if (!Q_stricmp(key, "_cone")) e.cone = (float)atof(val);
			else if (!Q_stricmp(key, "_focus")) e.focus = (float)atof(val);
			else if (!Q_stricmp(key, "_fade")) e.fade = (float)atof(val);
			else if (!Q_stricmp(key, "style")) e.style = atoi(val);
		}

		if (num == cap)
		{
			lsent_t *n;

			cap = cap ? cap * 2 : 256;
			n = realloc(list, cap * sizeof(*list));

			if (!n)
			{
				break;
			}

			list = n;
		}

		list[num++] = e;
	}

	*count = num;
	return list;
}

/* the SURF_LIGHT face whose plane passes closest to a patch: its outward normal */
static qboolean
PatchNormal(const gl3model_t *mod, const vec3_t o, vec3_t n)
{
	float best = 1e9f;
	int i;

	for (i = 0; i < mod->numsurfaces; i++)
	{
		const msurface_t *s = &mod->surfaces[i];
		const glpoly_t *p = s->polys;
		vec3_t c = {0, 0, 0}, pn, dc;
		float d;
		int k;

		if (!s->texinfo || !(s->texinfo->flags & SURF_LIGHT) || (s->texinfo->flags & SURF_SKY) || !p ||
			(p->numverts < 3) || !s->plane)
		{
			continue;
		}

		for (k = 0; k < p->numverts; k++)
		{
			VectorAdd(c, p->vertices[k].pos, c);
		}

		VectorScale(c, 1.0f / p->numverts, c);
		VectorCopy(s->plane->normal, pn);
		d = s->plane->dist;

		if (s->flags & SURF_PLANEBACK)
		{
			VectorNegate(pn, pn);
			d = -d;
		}

		VectorSubtract(o, c, dc);
		d = fabsf(DotProduct(o, pn) - d) + 0.01f * VectorLength(dc);

		if (d < best)
		{
			best = d;
			VectorCopy(pn, n);
		}
	}

	return best < 1e8f;
}

void
GL3_LShadow_LoadMap(const gl3model_t *mod, const char *ents, int entlen, const byte *lump, int len)
{
	int i, j, nents = 0, n = len / 36, npatch = 0;
	lsent_t *e = ParseEntities(ents, entlen, &nents);

	free(lights);
	lights = NULL;
	numlights = 0;
	L.built = NULL;

	for (i = 0; i < LS_MAXSLOTS; i++)
	{
		L.slot[i] = -1;
	}

	if (n > 0)
	{
		lights = malloc(sizeof(lslight_t) * n);
	}

	for (i = 0; lights && (i < n); i++)
	{
		const byte *r = lump + 36 * i;
		lslight_t lt;
		int type = LittleLong(*(const int *)(r + 28));

		memset(&lt, 0, sizeof(lt));

		for (j = 0; j < 3; j++)
		{
			lt.o[j] = LittleFloat(*(const float *)(r + 4 * j));
			lt.color[j] = LittleFloat(*(const float *)(r + 12 + 4 * j));
		}

		lt.intensity = LittleFloat(*(const float *)(r + 24));
		lt.fade = 1;

		if (lt.intensity <= 0)
		{
			continue;
		}

		if ((type == 1) || (type == 2))
		{
			lt.kind = LS_POINT;

			for (j = 0; j < nents; j++)
			{
				const lsent_t *le = &e[j];
				int k;

				if (!le->hasorigin || Q_strncasecmp(le->classname, "light", 5) || (fabsf(le->origin[0] - lt.o[0]) >= 1) ||
					(fabsf(le->origin[1] - lt.o[1]) >= 1) || (fabsf(le->origin[2] - lt.o[2]) >= 1))
				{
					continue;
				}

				lt.fade = (le->fade > 0) ? le->fade : 1;
				lt.style = le->style;

				if (type == 2)
				{
					/* as the compiler: the axis to the target, else _mangle (yaw pitch), else angle (-1 up, -2 down) */
					qboolean found = false;

					lt.kind = LS_SPOT;
					lt.stopdot = cosf(((le->cone > 0) ? le->cone : 10) * (float)M_PI / 180);
					lt.focus = (le->focus > 0) ? le->focus : 1;

					for (k = 0; le->target[0] && (k < nents); k++)
					{
						if (e[k].hasorigin && !strcmp(e[k].targetname, le->target))
						{
							VectorSubtract(e[k].origin, lt.o, lt.dir);
							found = true;
							break;
						}
					}

					if (!found && le->hasmangle)
					{
						float y = le->mangle[0] * (float)M_PI / 180, pt = le->mangle[1] * (float)M_PI / 180;

						VectorSet(lt.dir, cosf(pt) * cosf(y), cosf(pt) * sinf(y), sinf(pt));
						found = true;
					}

					if (!found)
					{
						float y = (le->hasangle ? le->angle : 0) * (float)M_PI / 180;

						if (le->hasangle && (le->angle == -1)) VectorSet(lt.dir, 0, 0, 1);
						else if (le->hasangle && (le->angle == -2)) VectorSet(lt.dir, 0, 0, -1);
						else VectorSet(lt.dir, cosf(y), sinf(y), 0);
					}

					if (VectorNormalize(lt.dir) == 0)
					{
						VectorSet(lt.dir, 0, 0, -1);
					}
				}

				break;
			}

			/* linear falloff: dark where intensity - fade x distance reaches 0 */
			lt.radius = fminf(lt.intensity / lt.fade, 1200.0f);

			if (lt.radius < 48)
			{
				continue;
			}

			lights[numlights++] = lt;
		}
		else if ((type == 0) && mod)
		{
			/* a panel: patches of one face direction within 64 units become one emitter */
			vec3_t pn;
			int best = -1;

			if (!PatchNormal(mod, lt.o, pn))
			{
				continue;
			}

			for (j = 0; j < numlights; j++)
			{
				lslight_t *c = &lights[j];
				vec3_t d;

				if ((c->kind != LS_SURF) || (DotProduct(c->n, pn) < 0.95f))
				{
					continue;
				}

				VectorSubtract(c->o, lt.o, d);

				if (VectorLength(d) < 64)
				{
					best = j;
					break;
				}
			}

			if (best >= 0)
			{
				lslight_t *c = &lights[best];
				float w = lt.intensity / (c->intensity + lt.intensity);

				for (j = 0; j < 3; j++)
				{
					c->o[j] += (lt.o[j] - c->o[j]) * w;
					c->color[j] += (lt.color[j] - c->color[j]) * w;
				}

				c->intensity += lt.intensity;
			}
			else
			{
				lt.kind = LS_SURF;
				VectorCopy(pn, lt.n);
				lights[numlights++] = lt;
			}

			npatch++;
		}
	}

	/* panels: the shadow cube a little off the face; reach where 1/d light falls below a few lightmap units */
	for (i = 0; i < numlights; i++)
	{
		lslight_t *l = &lights[i];

		if (l->kind != LS_SURF)
		{
			continue;
		}

		VectorMA(l->o, 6, l->n, l->o);
		l->radius = Q_clamp(sqrtf(l->intensity) * 3.0f, 96.0f, 900.0f);
	}

	free(e);
	R_Printf(PRINT_DEVELOPER, "lamp shadows: %d lamps (%d surface patches merged)\n", numlights, npatch);
}

/* ------------------------------------------------------------------
 * the receiving side (prepended to the world shaders with the sun's, gl3_csm.c)
 * ------------------------------------------------------------------ */

static const char *ls_glsl =
	"layout (std140) uniform uniOMls\n"
	"{\n"
	"	mat4 omLsVP[48];\n"
	"	vec4 omLsPos[8];\n"
	"	vec4 omLsCol[8];\n"
	"	vec4 omLsA[8];\n"
	"	vec4 omLsDir[8];\n"
	"	vec4 omLsCone[8];\n"
	"	vec4 omLsP;\n"
	"	vec4 omLsP2;\n"
	"};\n"
	"uniform highp sampler2DArrayShadow omLsMap;\n"
	"void omLsApply(inout vec3 lm, vec3 wp, vec3 nrm)\n"
	"{\n"
	"	int n = int(omLsP.x + 0.5);\n"
	"	if (n <= 0) return;\n"
	"	vec3 N = normalize(nrm);\n"
	"	float lum = max(max(lm.r, lm.g), lm.b);\n"
	"	float keep = 1.0, total = 0.0;\n"
	"	vec3 dbg = vec3(0.0);\n"
	"	float cl[8];\n"
	"	vec4 uv[8];\n"   /* face texture position, layer, depth */
	"	for (int i = 0; i < 8; i++)\n"
	"	{\n"
	"		cl[i] = 0.0;\n"
	"		if (i >= n) break;\n"
	"		vec3 L = omLsPos[i].xyz - wp;\n"
	"		float d = length(L);\n"
	"		if (d >= omLsPos[i].w || d < 1.0) continue;\n"
	"		vec3 l = L / d;\n"
	"		float ndl = dot(N, l);\n"
	"		if (ndl <= 0.0) continue;\n"
	/* this lamp's light here in lightmap units, as the compiler made it */
	"		float c;\n"
	"		if (omLsCol[i].w > 1.5)\n"
	"		{\n"
	"			float ce = dot(omLsA[i].xyz, -l);\n"
	"			if (ce <= 0.0) continue;\n"
	"			c = omLsA[i].w * ndl * ce / max(d, 16.0);\n"
	"		}\n"
	"		else\n"
	"		{\n"
	"			c = max(omLsA[i].x - d * omLsA[i].y, 0.0) * ndl;\n"
	"			if (omLsCol[i].w > 0.5)\n"
	"			{\n"
	"				float ca = dot(-l, omLsDir[i].xyz);\n"
	"				if (ca < omLsA[i].w || ca <= 0.0) continue;\n"
	"				c *= pow(ca, omLsA[i].z);\n"
	"			}\n"
	"		}\n"
	"		vec3 crgb = omLsCol[i].rgb * c;\n"
	"		float ci = max(max(crgb.r, crgb.g), crgb.b);\n"
	"		if (ci < 0.02 * lum || ci < 0.002) continue;\n"
	"		total += ci;\n"
	/* only inside the cone from the lamp around its casters and past the nearest of them can it be shadowed */
	"		if (dot(-l, omLsCone[i].xyz) < omLsCone[i].w || d < omLsDir[i].w) continue;\n"
	"		cl[i] = ci;\n"
	"	}\n"
	"	if (total <= 0.0) return;\n"
	/* a lamp's share: its light against the lightmap (r_om_lshadow_gain over), never more than its part of the
	   lamps here - four panels over a floor give a quarter each, a lone lamp all */
	"	for (int i = 0; i < 8; i++)\n"
	"	{\n"
	"		if (i >= n) break;\n"
	"		if (cl[i] <= 0.0) continue;\n"
	/* a texel and a half off the face and toward the lamp (the static faces hold this face too) */
	"		vec3 L = omLsPos[i].xyz - wp;\n"
	"		float d = length(L);\n"
	"		vec3 p = wp + (N + L / d) * (1.5 * d * omLsP.w);\n"
	"		vec3 v = p - omLsPos[i].xyz;\n"
	"		vec3 a = abs(v);\n"
	"		int f = (a.x >= a.y && a.x >= a.z) ? (v.x < 0.0 ? 1 : 0) : ((a.y >= a.z) ? (v.y < 0.0 ? 3 : 2) : (v.z < 0.0 ? 5 : 4));\n"
	"		vec4 sc = omLsVP[i * 6 + f] * vec4(p, 1.0);\n"
	"		uv[i] = vec4(sc.xyz / sc.w * 0.5 + 0.5, float(i * 12 + f)).xywz;\n"
	/* the lightmap has this lamp's light only where the static world lets it through */
	"		if (texture(omLsMap, uv[i]) < 0.5) continue;\n"
	"		float share = min(cl[i] / max(lum, 0.004), cl[i] / total);\n"
	/* four bilinear compare taps a texel apart: a 4 x 4 texel filter */
	"		vec2 t = vec2(omLsP2.x);\n"
	"		float s = texture(omLsMap, vec4(uv[i].xy + vec2(-t.x, -t.y), uv[i].z + 6.0, uv[i].w))\n"
	"			+ texture(omLsMap, vec4(uv[i].xy + vec2(t.x, -t.y), uv[i].z + 6.0, uv[i].w))\n"
	"			+ texture(omLsMap, vec4(uv[i].xy + vec2(-t.x, t.y), uv[i].z + 6.0, uv[i].w))\n"
	"			+ texture(omLsMap, vec4(uv[i].xy + vec2(t.x, t.y), uv[i].z + 6.0, uv[i].w));\n"
	"		float sh = 1.0 - s * 0.25;\n"
	"		dbg = max(dbg, vec3(sh * share, share, sh) * vec3(1.0, 0.5, 0.25));\n"
	"		keep *= 1.0 - omLsP.y * share * sh;\n"
	"	}\n"
	/* r_om_lshadow_debug: red = the darkening (share x blocked), green = a lamp's share of the light, blue = a
	   caster blocks a lamp */
	"	if (omLsP2.y > 0.5) { lm = dbg; return; }\n"
	"	lm *= max(keep, omLsP.z);\n"
	"}\n";

const char *
GL3_LShadow_Glsl(void)
{
	return ls_glsl;
}

/* with prog in use: the block binding and the sampler unit */
void
GL3_LShadow_BindProgram(GLuint prog)
{
	GLuint bi = glGetUniformBlockIndex(prog, "uniOMls");
	GLint loc = glGetUniformLocation(prog, "omLsMap");

	if (bi != GL_INVALID_INDEX)
	{
		glUniformBlockBinding(prog, bi, LS_BINDING);
	}

	/* the world shaders are made before GL3_LShadow_Init */
	if (!L.maxunits)
	{
		glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &L.maxunits);
	}

	/* without a 17th unit the sampler shares the sun's array type on its unit (never read: no slots) */
	if (loc != -1)
	{
		glUniform1i(loc, L.maxunits > LS_UNIT ? LS_UNIT : 8);
	}
}

/* ------------------------------------------------------------------
 * GL objects
 * ------------------------------------------------------------------ */

static void
UploadUBO(void)
{
	gl3state.currentUBO = L.ubo;
	glBindBuffer(GL_UNIFORM_BUFFER, L.ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(L.u), &L.u, GL_DYNAMIC_DRAW);
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
	if (L.maxunits <= LS_UNIT)
	{
		return;
	}

	GL3_SelectTMU(GL_TEXTURE0 + LS_UNIT);
	glBindTexture(GL_TEXTURE_2D_ARRAY, L.tex ? L.tex : L.dummy);
	GL3_SelectTMU(GL_TEXTURE0);
}

void
GL3_LShadow_Init(void)
{
	int i;

	r_om_lshadow = ri.Cvar_Get("r_om_lshadow", "1", CVAR_ARCHIVE);
	r_om_lshadow_lights = ri.Cvar_Get("r_om_lshadow_lights", "4", CVAR_ARCHIVE);
	r_om_lshadow_size = ri.Cvar_Get("r_om_lshadow_size", "512", CVAR_ARCHIVE);
	r_om_lshadow_strength = ri.Cvar_Get("r_om_lshadow_strength", "0.85", CVAR_ARCHIVE);
	r_om_lshadow_range = ri.Cvar_Get("r_om_lshadow_range", "1500", CVAR_ARCHIVE);
	r_om_lshadow_debug = ri.Cvar_Get("r_om_lshadow_debug", "0", 0);
	r_om_lshadow_gain = ri.Cvar_Get("r_om_lshadow_gain", "4", CVAR_ARCHIVE);

	memset(&L.u, 0, sizeof(L.u));
	L.failed = L.active = L.capturing = false;
	L.tex = L.fbo = 0;
	L.size = L.nslots = 0;
	L.built = NULL;

	for (i = 0; i < LS_MAXSLOTS; i++)
	{
		L.slot[i] = -1;
	}

	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &L.maxunits);
	ri.Cmd_AddCommand("r_om_lshadow_info", GL3_LShadow_Info);

	glGenBuffers(1, &L.ubo);
	UploadUBO();
	glBindBufferBase(GL_UNIFORM_BUFFER, LS_BINDING, L.ubo);
	L.uboenabled = false;

	if (L.maxunits > LS_UNIT)
	{
		glGenTextures(1, &L.dummy);
		GL3_SelectTMU(GL_TEXTURE0 + LS_UNIT);
		DepthArray(L.dummy, 1, LS_MAXSLOTS * 12);
		GL3_SelectTMU(GL_TEXTURE0);
		BindMap();
	}
	else
	{
		R_Printf(PRINT_ALL, "lamp shadows: %d texture units, need %d - off\n", L.maxunits, LS_UNIT + 1);
		L.failed = true;
	}
}

void
GL3_LShadow_Shutdown(void)
{
	if (L.tex) glDeleteTextures(1, &L.tex);
	if (L.dummy) glDeleteTextures(1, &L.dummy);
	if (L.fbo) glDeleteFramebuffers(1, &L.fbo);
	if (L.ubo) glDeleteBuffers(1, &L.ubo);
	if (L.prog) glDeleteProgram(L.prog);
	if (L.vao) glDeleteVertexArrays(1, &L.vao);
	if (L.vbo) glDeleteBuffers(1, &L.vbo);
	free(lights);
	lights = NULL;
	numlights = 0;
	memset(&L, 0, sizeof(L));
	ri.Cmd_RemoveCommand("r_om_lshadow_info");
}

static GLuint
Compile(GLenum type, const char *src)
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
		glDeleteShader(s);
		return 0;
	}

	return s;
}

/* the depth program of the casters and their buffer (all of a frame's casters, uploaded once) */
static qboolean
CreateObjects(void)
{
	GLuint vs, fs;
	GLint ok = 0;

	if (L.prog)
	{
		return true;
	}

	vs = Compile(GL_VERTEX_SHADER, "in vec3 position;\nuniform mat4 vp;\nvoid main() { gl_Position = vp * vec4(position, 1.0); }\n");
	fs = Compile(GL_FRAGMENT_SHADER, "out vec4 outColor;\nvoid main() { outColor = vec4(1.0); }\n");

	if (!vs || !fs)
	{
		return false;
	}

	L.prog = glCreateProgram();
	glAttachShader(L.prog, vs);
	glAttachShader(L.prog, fs);
	glBindAttribLocation(L.prog, GL3_ATTRIB_POSITION, "position");
#ifndef YQ2_GL3_GLES3
	glBindFragDataLocation(L.prog, 0, "outColor");
#endif
	glLinkProgram(L.prog);
	glDeleteShader(vs);
	glDeleteShader(fs);
	glGetProgramiv(L.prog, GL_LINK_STATUS, &ok);

	if (!ok)
	{
		glDeleteProgram(L.prog);
		L.prog = 0;
		return false;
	}

	L.u_vp = glGetUniformLocation(L.prog, "vp");
	glGenVertexArrays(1, &L.vao);
	glGenBuffers(1, &L.vbo);
	GL3_BindVAO(L.vao);
	GL3_BindVBO(L.vbo);
	glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
	qglVertexAttribPointer(GL3_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);
	GL3_BindVAO(0);
	return true;
}

static qboolean
CreateMap(int size, int nslots)
{
	GLenum st;
	int i;

	if (L.tex && (L.size == size) && (L.nslots == nslots))
	{
		return true;
	}

	if (!L.tex)
	{
		glGenTextures(1, &L.tex);
	}

	if (!L.fbo)
	{
		glGenFramebuffers(1, &L.fbo);
	}

	GL3_SelectTMU(GL_TEXTURE0 + LS_UNIT);
	DepthArray(L.tex, size, 12 * nslots);
	GL3_SelectTMU(GL_TEXTURE0);

	glBindFramebuffer(GL_FRAMEBUFFER, L.fbo);
	glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, L.tex, 0, 0);
	{
		GLenum none = GL_NONE;

		glDrawBuffers(1, &none);
		glReadBuffer(GL_NONE);
	}
	st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (st != GL_FRAMEBUFFER_COMPLETE)
	{
		R_Printf(PRINT_ALL, "lamp shadows: framebuffer incomplete (0x%x), off\n", st);
		glDeleteTextures(1, &L.tex);
		L.tex = 0;
		return false;
	}

	L.size = size;
	L.nslots = nslots;

	for (i = 0; i < LS_MAXSLOTS; i++)
	{
		L.slot[i] = -1;
	}

	return true;
}

/* ------------------------------------------------------------------
 * per frame
 * ------------------------------------------------------------------ */

static qboolean
Want(void)
{
	if (!r_om_lshadow || !r_om_lshadow->value || L.failed || !gl3_worldmodel || !numlights)
	{
		return false;
	}

	if (r_newrefdef.rdflags & (RDF_NOWORLDMODEL | SOF_RDF_IRGOGGLES))
	{
		return false;
	}

	return !r_fullbright->value;
}

/* whether the Ghoul models of this frame are kept as casters */
qboolean
GL3_LShadow_Capturing(void)
{
	return L.capturing;
}

static float
StyleScale(const lslight_t *l)
{
	const float *rgb;

	if (!r_newrefdef.lightstyles || (l->style < 0) || (l->style >= MAX_LIGHTSTYLES))
	{
		return 1;
	}

	rgb = r_newrefdef.lightstyles[l->style].rgb;
	return (rgb[0] + rgb[1] + rgb[2]) / 3.0f;
}

/* face f of the cube around o (0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z), column-major projection x view */
static void
FaceMatrix(int f, const vec3_t o, float farz, float *m)
{
	static const float fwd[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	static const float upv[6][3] = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
	vec3_t F, U, S;
	float A = (farz + LS_NEAR) / (LS_NEAR - farz), B = 2 * farz * LS_NEAR / (LS_NEAR - farz), it = 1.0f / LS_TAN;

	VectorCopy(fwd[f], F);
	VectorCopy(upv[f], U);
	CrossProduct(F, U, S);
	VectorNormalize(S);
	CrossProduct(S, F, U);

	m[0] = S[0] * it;
	m[4] = S[1] * it;
	m[8] = S[2] * it;
	m[12] = -DotProduct(S, o) * it;
	m[1] = U[0] * it;
	m[5] = U[1] * it;
	m[9] = U[2] * it;
	m[13] = -DotProduct(U, o) * it;
	m[2] = -F[0] * A;
	m[6] = -F[1] * A;
	m[10] = -F[2] * A;
	m[14] = DotProduct(F, o) * A + B;
	m[3] = F[0];
	m[7] = F[1];
	m[11] = F[2];
	m[15] = -DotProduct(F, o);
}

/* distance from a lamp to a caster's box, -1 when the lamp is inside it (a lamp model, a light carried: it
   shadows nothing sensible) */
static float
SegDist(const csmseg_t *s, const vec3_t o)
{
	float d2 = 0;
	qboolean inside = true;
	int k;

	for (k = 0; k < 3; k++)
	{
		float v = 0;

		if (o[k] < s->mins[k] - 4) { v = s->mins[k] - o[k]; inside = false; }
		else if (o[k] > s->maxs[k] + 4) { v = o[k] - s->maxs[k]; inside = false; }

		d2 += v * v;
	}

	return inside ? -1.0f : sqrtf(d2);
}

static qboolean
SegNear(const csmseg_t *s, const vec3_t o, float r)
{
	float d = SegDist(s, o);

	return (d >= 0) && (d < r);
}

/* whether a caster's box reaches into face f of the cube around o (the face sees |minor| <= LS_TAN x major) */
static qboolean
SegInFace(const csmseg_t *s, const vec3_t o, int f)
{
	int ax = f >> 1, a1 = (ax + 1) % 3, a2 = (ax + 2) % 3, c;
	float sg = (f & 1) ? -1.0f : 1.0f;
	qboolean out0 = true, out1 = true, out2 = true, out3 = true;

	for (c = 0; c < 8; c++)
	{
		vec3_t p;
		float m;

		p[0] = ((c & 1) ? s->maxs[0] : s->mins[0]) - o[0];
		p[1] = ((c & 2) ? s->maxs[1] : s->mins[1]) - o[1];
		p[2] = ((c & 4) ? s->maxs[2] : s->mins[2]) - o[2];
		m = p[ax] * sg * LS_TAN;
		out0 &= (m - p[a1] < 0);
		out1 &= (m + p[a1] < 0);
		out2 &= (m - p[a2] < 0);
		out3 &= (m + p[a2] < 0);
	}

	return !(out0 || out1 || out2 || out3);
}

/* a lamp's light at distance d in lightmap units (the compiler's falloff, no angles) */
static float
LampLight(const lslight_t *l, float d)
{
	if (l->kind == LS_SURF)
	{
		return l->intensity * 0.012f / Q_max(d, 16.0f);
	}

	return Q_max(l->intensity - d * l->fade, 0.0f);
}

/* before SetupGL of a world view: which lamps cast, their depth cubes */
void
GL3_LShadow_Frame(void)
{
	const float *tris;
	const csmseg_t *segs;
	int nsegs, size, nslots, i, j, k, top = 0;
	int pick[LS_MAXSLOTS];
	float score[LS_MAXSLOTS], range;
	GLint oldfunc;
	GLfloat oldrange[2];
	GLboolean wasblend, wascull, wasoffset;

	L.active = false;
	L.capturing = Want();
	L.nframes++;

	if (!L.capturing)
	{
		if (L.uboenabled)
		{
			L.u.params[0] = 0;
			UploadUBO();
			L.uboenabled = false;
		}

		return;
	}

	size = (int)r_om_lshadow_size->value;
	size = (size >= 1024) ? 1024 : (size >= 512) ? 512 : 256;
	nslots = Q_clamp((int)r_om_lshadow_lights->value, 1, LS_MAXSLOTS);

	if (!CreateObjects() || !CreateMap(size, nslots))
	{
		L.failed = true;
		L.capturing = false;
		return;
	}

	if (L.built != gl3_worldmodel)
	{
		for (i = 0; i < LS_MAXSLOTS; i++)
		{
			L.slot[i] = -1;
		}

		L.built = gl3_worldmodel;
	}

	nsegs = GL3_CSM_Casters(&tris, &segs);
	range = Q_max(r_om_lshadow_range->value, 256.0f);

	/* the lamps by their light on the nearest caster in reach, less with distance to the eye; a lamp that has
	   a slot keeps it against a slightly better one (no flicker, no redrawn static faces) */
	for (i = 0; i < numlights && nsegs > 0; i++)
	{
		const lslight_t *l = &lights[i];
		vec3_t d;
		float dist, s, cd = 1e30f;
		qboolean held = false;

		VectorSubtract(l->o, r_newrefdef.vieworg, d);
		dist = VectorLength(d);

		if ((dist > l->radius + range) || (StyleScale(l) < 0.05f))
		{
			continue;
		}

		for (j = 0; j < nsegs; j++)
		{
			float sd = SegDist(&segs[j], l->o);

			if ((sd >= 0) && (sd < l->radius))
			{
				cd = Q_min(cd, sd);
			}
		}

		if (cd > 1e29f)
		{
			continue;
		}

		for (j = 0; j < nslots; j++)
		{
			held |= (L.slot[j] == i);
		}

		s = LampLight(l, cd) * StyleScale(l) / (dist + 256.0f) * (held ? 1.5f : 1.0f);

		if (s <= 0)
		{
			continue;
		}

		for (j = top; j > 0 && score[j - 1] < s; j--)
		{
			if (j < nslots)
			{
				score[j] = score[j - 1];
				pick[j] = pick[j - 1];
			}
		}

		if (j >= nslots)
		{
			continue;
		}

		score[j] = s;
		pick[j] = i;

		if (top < nslots)
		{
			top++;
		}
	}

	/* slots: kept lamps stay where they are, new ones take the free slots */
	for (j = 0; j < nslots; j++)
	{
		qboolean keep = false;

		for (k = 0; k < top; k++)
		{
			keep |= (pick[k] == L.slot[j]);
		}

		if (!keep)
		{
			L.slot[j] = -1;
		}
	}

	for (k = 0; k < top; k++)
	{
		qboolean has = false;

		for (j = 0; j < nslots; j++)
		{
			has |= (L.slot[j] == pick[k]);
		}

		for (j = 0; !has && (j < nslots); j++)
		{
			if (L.slot[j] < 0)
			{
				L.slot[j] = pick[k];
				L.dirty[j] = true;
				has = true;
			}
		}
	}

	for (j = nslots; j < LS_MAXSLOTS; j++)
	{
		L.slot[j] = -1;
	}

	/* the uniform block */
	memset(L.u.pos, 0, sizeof(L.u.pos));
	top = 0;

	for (j = 0; j < nslots; j++)
	{
		const lslight_t *l;
		float sc;

		if (L.slot[j] < 0)
		{
			continue;
		}

		l = &lights[L.slot[j]];
		/* r_om_lshadow_gain: a room lit by many panels gives each a tenth of the light, a true shadow of one
		   would not show; a lamp's share counts that many times over (a lamp that lights the place alone is
		   at full share already) */
		sc = StyleScale(l) / 255.0f * Q_clamp(r_om_lshadow_gain->value, 1.0f, 10.0f);
		top = j + 1;

		for (k = 0; k < 6; k++)
		{
			FaceMatrix(k, l->o, l->radius + 16, L.u.vp[j * 6 + k]);
		}

		L.u.pos[j][0] = l->o[0];
		L.u.pos[j][1] = l->o[1];
		L.u.pos[j][2] = l->o[2];
		L.u.pos[j][3] = l->radius;
		L.u.col[j][0] = l->color[0] * sc;
		L.u.col[j][1] = l->color[1] * sc;
		L.u.col[j][2] = l->color[2] * sc;
		L.u.col[j][3] = (float)l->kind;

		if (l->kind == LS_SURF)
		{
			/* omlight's inverse-linear patch light (intensity x cos x cos / d x 0.01) at its fitted weight (~1.2) */
			L.u.a[j][0] = l->n[0];
			L.u.a[j][1] = l->n[1];
			L.u.a[j][2] = l->n[2];
			L.u.a[j][3] = l->intensity * 0.012f;
		}
		else
		{
			L.u.a[j][0] = l->intensity;
			L.u.a[j][1] = l->fade;
			L.u.a[j][2] = l->focus;
			L.u.a[j][3] = l->stopdot;
			L.u.dir[j][0] = l->dir[0];
			L.u.dir[j][1] = l->dir[1];
			L.u.dir[j][2] = l->dir[2];
		}

		/* the cone around the casters in reach (their box corners), a few degrees wider for the filter; past
		   a right angle it covers everything */
		{
			vec3_t axis = {0, 0, 0};
			float mind = 1e30f, cmin = 1;

			for (i = 0; i < nsegs; i++)
			{
				int c;

				if (!SegNear(&segs[i], l->o, l->radius))
				{
					continue;
				}

				mind = Q_min(mind, SegDist(&segs[i], l->o));

				for (c = 0; c < 8; c++)
				{
					vec3_t p;

					p[0] = ((c & 1) ? segs[i].maxs[0] : segs[i].mins[0]) - l->o[0];
					p[1] = ((c & 2) ? segs[i].maxs[1] : segs[i].mins[1]) - l->o[1];
					p[2] = ((c & 4) ? segs[i].maxs[2] : segs[i].mins[2]) - l->o[2];
					VectorNormalize(p);
					VectorAdd(axis, p, axis);
				}
			}

			if (VectorNormalize(axis) > 0)
			{
				for (i = 0; i < nsegs; i++)
				{
					int c;

					if (!SegNear(&segs[i], l->o, l->radius))
					{
						continue;
					}

					for (c = 0; c < 8; c++)
					{
						vec3_t p;

						p[0] = ((c & 1) ? segs[i].maxs[0] : segs[i].mins[0]) - l->o[0];
						p[1] = ((c & 2) ? segs[i].maxs[1] : segs[i].mins[1]) - l->o[1];
						p[2] = ((c & 4) ? segs[i].maxs[2] : segs[i].mins[2]) - l->o[2];
						VectorNormalize(p);
						cmin = Q_min(cmin, DotProduct(p, axis));
					}
				}

				cmin = cosf(Q_min(acosf(Q_clamp(cmin, -1.0f, 1.0f)) + 0.08f, (float)M_PI));
			}
			else
			{
				cmin = -1;
			}

			L.u.cone[j][0] = axis[0];
			L.u.cone[j][1] = axis[1];
			L.u.cone[j][2] = axis[2];
			L.u.cone[j][3] = (cmin < 0) ? -2.0f : cmin;
			L.u.dir[j][3] = (mind < 1e29f) ? Q_max(mind - 8.0f, 0.0f) : 0.0f;
		}
	}

	L.u.params[0] = (float)top;
	L.u.params[1] = Q_clamp(r_om_lshadow_strength->value, 0.0f, 1.0f);
	L.u.params[2] = 0.2f;
	L.u.params[3] = 2.0f * LS_TAN / size;
	L.u.params2[0] = 1.0f / size;
	L.u.params2[1] = r_om_lshadow_debug->value ? 1.0f : 0.0f;
	UploadUBO();
	L.uboenabled = true;

	if (!top)
	{
		return;
	}

	/* the depth cubes */
	glGetIntegerv(GL_DEPTH_FUNC, &oldfunc);
	glGetFloatv(GL_DEPTH_RANGE, oldrange);
	wasblend = glIsEnabled(GL_BLEND);
	wascull = glIsEnabled(GL_CULL_FACE);
	wasoffset = glIsEnabled(GL_POLYGON_OFFSET_FILL);

	glBindFramebuffer(GL_FRAMEBUFFER, L.fbo);
	glViewport(0, 0, size, size);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDepthRange(0, 1);
	glDepthMask(GL_TRUE);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(1.5f, 2.0f);

	/* all the casters once; each face draws the runs of primitives whose boxes reach into it */
	{
		int nverts = 0;

		for (i = 0; i < nsegs; i++)
		{
			nverts = Q_max(nverts, segs[i].first + segs[i].num);
		}

		GL3_BindVAO(L.vao);
		GL3_BindVBO(L.vbo);
		glBufferData(GL_ARRAY_BUFFER, nverts * 3 * sizeof(float), NULL, GL_STREAM_DRAW);
		glBufferSubData(GL_ARRAY_BUFFER, 0, nverts * 3 * sizeof(float), tris);
	}

	for (j = 0; j < nslots; j++)
	{
		const lslight_t *l;

		if (L.slot[j] < 0)
		{
			continue;
		}

		l = &lights[L.slot[j]];
		L.nstatic += L.dirty[j] ? 1 : 0;

		for (k = 0; k < 6; k++)
		{
			static const float one = 1.0f;
			const float *vp = L.u.vp[j * 6 + k];
			int first = -1, num = 0;

			if (L.dirty[j])
			{
				glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, L.tex, 0, j * 12 + k);
				glClearBufferfv(GL_DEPTH, 0, &one);
				GL3_CSM_DrawDepth(vp, NULL, 0, true);
			}

			glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, L.tex, 0, j * 12 + 6 + k);
			glClearBufferfv(GL_DEPTH, 0, &one);
			GL3_UseProgram(L.prog);
			glUniformMatrix4fv(L.u_vp, 1, GL_FALSE, vp);
			GL3_BindVAO(L.vao);

			for (i = 0; i <= nsegs; i++)
			{
				qboolean in = (i < nsegs) && SegNear(&segs[i], l->o, l->radius) && SegInFace(&segs[i], l->o, k);

				/* a run grows while the next primitive follows on in the buffer */
				if (in && (first >= 0) && (segs[i].first == first + num))
				{
					num += segs[i].num;
					continue;
				}

				if (num >= 3)
				{
					glDrawArrays(GL_TRIANGLES, first, num);
					L.ndynverts += num;
				}

				first = in ? segs[i].first : -1;
				num = in ? segs[i].num : 0;
			}
		}

		L.dirty[j] = false;
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
	L.active = true;
}

/* r_om_lshadow_info: the lamps in the slots */
void
GL3_LShadow_Info(void)
{
	static const char *kinds[] = {"lamp", "spot", "panel"};
	int j, n = 0;

	R_Printf(PRINT_ALL, "lamp shadows: %d lamps in the map, %s; since the last info %d views, %d static cubes, "
		"%.0f caster vertices a view\n", numlights, L.active ? "active" : "inactive", L.nframes, L.nstatic,
		L.nframes ? (float)L.ndynverts / L.nframes : 0.0f);
	L.nframes = L.nstatic = L.ndynverts = 0;

	for (j = 0; j < LS_MAXSLOTS; j++)
	{
		const lslight_t *l;

		if (L.slot[j] < 0)
		{
			continue;
		}

		l = &lights[L.slot[j]];
		R_Printf(PRINT_ALL, "  slot %d: %s %d at (%.0f %.0f %.0f) intensity %g radius %.0f style %d (%.2f)\n", j,
			kinds[l->kind], L.slot[j], l->o[0], l->o[1], l->o[2], l->intensity, l->radius, l->style, StyleScale(l));
		n++;
	}

	if (!n)
	{
		R_Printf(PRINT_ALL, "  no lamp has a moving caster in reach\n");
	}
}
