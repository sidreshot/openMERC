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
 * lightmap (the static faces let it through) and a caster blocks loses that lamp's light there and no more (the
 * subtractive shadow of baked lighting): the lamp's light after the compiler's own falloff (linear for lamps, 1/d
 * for panels) at the weights omlight fitted to the map, never more than the lamp's part of the light of all the
 * lamps that reach the caster. Between two lamps each shadow takes half; under many panels it is faint.
 *
 * The edge is percentage-closer soft shadows (Fernando, NVIDIA 2005): a blocker search around the point gives the
 * mean distance of the casters, the penumbra is the lamp's size x (receiver - blocker) / blocker, filtered at that
 * width - hard where the feet touch the floor, wide and faint away from the caster. The receivers are never in the
 * casters' maps, so a wide filter costs no acne. A lamp that gets or loses a slot fades in and out.
 */

#include "header/local.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define LS_MAXSLOTS 8
#define LS_BINDING 5        /* uniform block binding point (after uniOMcsm) */
/* the texture units of the depth cubes: with compare (filter taps) and through a sampler that reads the depth
   (blocker search). 16 / 17 where the GPU has 18 or more units; else (GLES guarantees 16, mobile GPUs have
   exactly that) 11 / 7, the two units the world, Ghoul, volumetric fog and beam shaders leave free - the fog and
   beam passes read the depth copy on unit 3 for it. r_om_lshadow_compact 1 forces 11 / 7 (a desktop test) */
static int ls_unit = 16, ls_rawunit = 17;
#define LS_UNIT ls_unit
#define LS_RAWUNIT ls_rawunit

static qboolean
LSEnoughUnits(int maxunits)
{
	cvar_t *compact = ri.Cvar_Get("r_om_lshadow_compact", "0", 0); /* read when the renderer starts (vid_restart) */

	if ((maxunits >= 18) && !compact->value)
	{
		ls_unit = 16;
		ls_rawunit = 17;
		return true;
	}

	ls_unit = 11;
	ls_rawunit = 7;
	return maxunits >= 12;
}
#define LS_TAN 1.08f        /* tan of half the face field of view: a little over 90 degrees for the filter taps */
#define LS_NEAR 2.0f
#define LS_FIXTURE 16.0f    /* a caster this close to its lamp is the lamp's own fixture or chain: no shadow */
#define LS_FADETIME 0.35f   /* seconds a lamp's shadow takes to come and go */

#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif

static cvar_t *r_om_lshadow, *r_om_lshadow_lights, *r_om_lshadow_size, *r_om_lshadow_strength;
static cvar_t *r_om_lshadow_debug, *r_om_lshadow_range, *r_om_lshadow_floor, *r_om_lshadow_lightsize;
static cvar_t *r_om_lshadow_softmax, *r_om_lshadow_taps, *r_om_lshadow_search;

/* the light model weights of the map (omlight's fit, else typical ones): lightmap units per unit of the model */
static float wpoint = 1.2f, wsurf = 0.85f, wspot = 0.6f;

/* sampler objects (GL 3.3, not in the loader): the depth read without compare on LS_RAWUNIT */
typedef void (APIENTRY *pfnGenSamplers)(GLsizei n, GLuint *s);
typedef void (APIENTRY *pfnDeleteSamplers)(GLsizei n, const GLuint *s);
typedef void (APIENTRY *pfnBindSampler)(GLuint unit, GLuint s);
typedef void (APIENTRY *pfnSamplerParameteri)(GLuint s, GLenum p, GLint v);
static pfnGenSamplers qglGenSamplers;
static pfnDeleteSamplers qglDeleteSamplers;
static pfnBindSampler qglBindSampler;
static pfnSamplerParameteri qglSamplerParameteri;

enum { LS_POINT, LS_SPOT, LS_SURF };

typedef struct
{
	vec3_t o, color;
	float intensity, fade, radius;
	int kind, style;
	vec3_t dir;          /* spotlight axis */
	float stopdot, focus;
	vec3_t n;            /* panel normal */
	vec3_t pmins, pmaxs; /* panel: the patches it is made of (its size) */
	float size;          /* across the emitter: the penumbra */
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
	float soft[LS_MAXSLOTS][4];  /* lamp size, fade in/out, its part of the light at the caster, - */
	float params[4];             /* slots, strength, darkest fraction, texel size per unit of distance */
	float params2[4];            /* 1 / size, debug view, blocker search (sampler objects), widest filter radius (uv) */
} lsubo_t;

static struct
{
	qboolean failed, active, capturing;
	int size, nslots;
	GLuint tex, dummy, fbo, ubo, prog, vao, vbo, sampler;
	GLint u_vp;
	int slot[LS_MAXSLOTS];          /* light index, -1 free */
	qboolean dirty[LS_MAXSLOTS];    /* the static faces need drawing */
	float fade[LS_MAXSLOTS];        /* 0..1: the shadow coming (picked) or going (dropped, the slot kept till 0) */
	float share[LS_MAXSLOTS];       /* the lamp's part of the light of all lamps at its caster */
	vec3_t shareref[LS_MAXSLOTS];   /* where that was measured */
	float lasttime;
	const gl3model_t *built;
	lsubo_t u;
	qboolean uboenabled;
	int maxunits;
	int nframes, nstatic, ndynverts;   /* r_om_lshadow_info: calls, static cubes drawn, caster vertices */
} L;

static lslight_t *lights;
static int numlights;

extern float (*gl3_ghoul_trace)(const float *start, const float *end);   /* gl3_ghoul.c: the engine's trace */

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
				AddPointToBounds(lt.o, c->pmins, c->pmaxs);
			}
			else
			{
				lt.kind = LS_SURF;
				VectorCopy(pn, lt.n);
				VectorCopy(lt.o, lt.pmins);
				VectorCopy(lt.o, lt.pmaxs);
				lights[numlights++] = lt;
			}

			npatch++;
		}
	}

	/* panels: the shadow cube a little off the face; reach where 1/d light falls below a few lightmap units; as
	   wide as its patches plus the one patch's own piece of face */
	for (i = 0; i < numlights; i++)
	{
		lslight_t *l = &lights[i];
		vec3_t ext;

		if (l->kind != LS_SURF)
		{
			continue;
		}

		VectorMA(l->o, 6, l->n, l->o);
		l->radius = Q_clamp(sqrtf(l->intensity) * 3.0f, 96.0f, 900.0f);
		VectorSubtract(l->pmaxs, l->pmins, ext);
		l->size = Q_clamp(VectorLength(ext) + 32.0f, 32.0f, 160.0f);
	}

	free(e);

	for (i = 0; i < LS_MAXSLOTS; i++)
	{
		L.fade[i] = 0;
		L.share[i] = 1;
	}

	/* the weights the map was lit with: omlight's fit when it explains the lightmaps and stays near the usual
	   values (a fit that put all the light on one kind of lamp is no measure of the others) */
	wpoint = 1.2f;
	wsurf = 0.85f;
	wspot = 0.6f;

	{
		float fp, fs, fsun, fspot, r2;
		float sunw = -1;   /* the sun's weight for gl3_csm.c, -1 none known */

		if (mod && GL3_Relight_Fit(mod->name, &fp, &fs, &fsun, &fspot, &r2) && (r2 >= 0.5f))
		{
			if ((fp >= wpoint * 0.4f) && (fp <= wpoint * 2.5f)) wpoint = fp;
			if ((fs >= wsurf * 0.4f) && (fs <= wsurf * 2.5f)) wsurf = fs;
			if ((fspot >= wspot * 0.4f) && (fspot <= wspot * 2.5f)) wspot = fspot;
			sunw = Q_max(fsun, 0.0f);
			R_Printf(PRINT_DEVELOPER, "lamp shadows: fitted weights point %.2f surface %.2f spot %.2f sun %.0f (R2 %.2f)\n",
				wpoint, wsurf, wspot, sunw, r2);
		}

		GL3_CSM_SetSunWeight(sunw);
	}

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
	"	vec4 omLsSoft[8];\n"
	"	vec4 omLsP;\n"
	"	vec4 omLsP2;\n"
	"};\n"
	"uniform highp sampler2DArrayShadow omLsMap;\n"
	"uniform highp sampler2DArray omLsRaw;\n"
	"const vec2 omLsDisk[16] = vec2[16](vec2(-0.942, -0.399), vec2(0.946, -0.769), vec2(-0.094, -0.929), vec2(0.345, 0.294),\n"
	"	vec2(-0.916, 0.458), vec2(-0.815, -0.879), vec2(-0.383, 0.277), vec2(0.975, 0.756), vec2(0.443, -0.975),\n"
	"	vec2(0.537, -0.474), vec2(-0.265, -0.419), vec2(0.792, 0.191), vec2(-0.242, 0.997), vec2(-0.814, 0.914),\n"
	"	vec2(0.200, 0.786), vec2(0.144, -0.141));\n"
	/* lm0: the lightmap before the sun's shadow too - together they never go below the floor of it */
	"void omLsApply(inout vec3 lm, vec3 lm0, vec3 wp, vec3 nrm)\n"
	"{\n"
	"	int n = int(omLsP.x + 0.5);\n"
	"	if (n <= 0) return;\n"
	"	vec3 N = normalize(nrm);\n"
	"	float lum = max(max(lm0.r, lm0.g), lm0.b);\n"
	"	float keep = 1.0, total = 0.0;\n"
	"	vec3 dbg = vec3(0.0);\n"
	"	float cl[8];\n"
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
	/* a face the lamp grazes holds a shadow stretched over texels: it fades out there */
	"		cl[i] = ci * smoothstep(0.02, 0.25, ndl);\n"
	"	}\n"
	"	if (total <= 0.0) return;\n"
	/* the filter taps turn per pixel (interleaved gradient noise): no banding of a wide penumbra */
	"	float K = omLsP.w / omLsP2.x;\n"   /* 2 tan: width of a face per unit of distance */
	"	float ang = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
	"	mat2 rot = mat2(cos(ang), sin(ang), -sin(ang), cos(ang));\n"
	"	for (int i = 0; i < 8; i++)\n"
	"	{\n"
	"		if (i >= n) break;\n"
	"		if (cl[i] <= 0.0) continue;\n"
	/* the lamp's light against all the light here (the lightmap), never more than its part of the lamps that
	   reach the caster: between two lamps half, under many panels little - too little to see, no taps */
	"		float share = min(min(cl[i] / max(lum, 0.004), cl[i] / total), omLsSoft[i].z);\n"
	"		if (omLsP.y * share * omLsSoft[i].y < 0.03) continue;\n"
	/* a texel and a half off the face and toward the lamp (the static faces hold this face too) */
	"		vec3 L = omLsPos[i].xyz - wp;\n"
	"		float d = length(L);\n"
	"		vec3 p = wp + (N + L / d) * (1.5 * d * omLsP.w);\n"
	"		vec3 v = p - omLsPos[i].xyz;\n"
	"		vec3 a = abs(v);\n"
	"		int f = (a.x >= a.y && a.x >= a.z) ? (v.x < 0.0 ? 1 : 0) : ((a.y >= a.z) ? (v.y < 0.0 ? 3 : 2) : (v.z < 0.0 ? 5 : 4));\n"
	"		vec4 sc = omLsVP[i * 6 + f] * vec4(p, 1.0);\n"
	"		vec3 uvz = sc.xyz / sc.w * 0.5 + 0.5;\n"
	"		float layer = float(i * 12 + f);\n"
	/* the lightmap has this lamp's light only where the static world lets it through */
	"		if (texture(omLsMap, vec4(uvz.xy, layer, uvz.z)) < 0.5) continue;\n"
	"		layer += 6.0;\n"
	"		float sh = 1.0;\n"
	"		int nt = int(omLsSoft[i].w + 0.5);\n"
	"		float zr = max(a.x, max(a.y, a.z));\n"   /* the receiver's distance along the face axis */
	"		float W = omLsSoft[i].x;\n"
	"		float zb = max(omLsDir[i].w, 4.0);\n"   /* the nearest caster, until the search finds the real ones */
	"		if (omLsP2.z > 0.5)\n"
	"		{\n"
	/* blocker search: where a caster could hide part of the lamp from here, out to the nearest caster */
	"			float fz = omLsPos[i].w + 16.0;\n"
	"			float sr = clamp(0.5 * W * max(zr - zb, 0.0) / (zr * K * zb), omLsP2.x, omLsP2.w);\n"
	"			float sum = 0.0, cnt = 0.0, zmax = 0.0;\n"
	"			for (int k = 0; k < nt; k += 2)\n"
	"			{\n"
	"				float dz = texture(omLsRaw, vec3(uvz.xy + rot * omLsDisk[k] * sr, layer)).r;\n"
	"				if (dz < uvz.z)\n"
	"				{\n"
	"					float z = 4.0 * fz / (fz + 2.0 - (2.0 * dz - 1.0) * (fz - 2.0));\n"   /* 2 n f / ..., near 2 */
	"					sum += z;\n"
	"					zmax = max(zmax, z);\n"
	"					cnt += 1.0;\n"
	"				}\n"
	"			}\n"
	"			if (cnt < 0.5) continue;\n"
	/* halfway to the caster nearest the receiver: the feet keep a hard contact though the body is in the search */
	"			zb = 0.5 * (sum / cnt + zmax);\n"
	/* every search tap blocked: inside the umbra (the filter would lie within the search), no filter */
	"			if (cnt > float(nt / 2) - 0.5) sh = 2.0;\n"
	"		}\n"
	/* the penumbra: lamp size x (receiver - blocker) / blocker, in the face's texture at the receiver */
	"		if (sh < 1.5)\n"
	"		{\n"
	"			float pr = clamp(0.5 * W * max(zr - zb, 0.0) / (zr * K * max(zb, 1.0)), omLsP2.x, omLsP2.w);\n"
	"			float s = 0.0;\n"
	"			for (int k = 0; k < nt; k++)\n"
	"				s += texture(omLsMap, vec4(uvz.xy + rot * omLsDisk[k] * pr, layer, uvz.z));\n"
	"			sh = 1.0 - s / float(nt);\n"
	"		}\n"
	"		else sh = 1.0;\n"
	"		float dk = omLsP.y * share * sh * omLsSoft[i].y;\n"
	"		dbg = max(dbg, vec3(dk, share, sh) * vec3(1.0, 0.5, 0.25));\n"
	"		keep *= 1.0 - dk;\n"
	"	}\n"
	/* r_om_lshadow_debug: red = the darkening, green = a lamp's share of the light, blue = a caster blocks a lamp */
	"	if (omLsP2.y > 0.5) { lm = dbg; return; }\n"
	"	if (keep < 1.0) lm = max(lm * keep, min(lm, lm0 * omLsP.z));\n"
	"}\n";

/* a GPU without the two texture units: the world shaders get an empty function */
static const char *ls_glsl_none =
	"void omLsApply(inout vec3 lm, vec3 lm0, vec3 wp, vec3 nrm) {}\n";

const char *
GL3_LShadow_GlslNone(void)
{
	return ls_glsl_none;
}

const char *
GL3_LShadow_Glsl(void)
{
	/* the world shaders are made before GL3_LShadow_Init */
	if (!L.maxunits)
	{
		glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &L.maxunits);
	}

	return LSEnoughUnits(L.maxunits) ? ls_glsl : ls_glsl_none;
}

/* with prog in use: the block binding and the sampler units */
void
GL3_LShadow_BindProgram(GLuint prog)
{
	GLuint bi = glGetUniformBlockIndex(prog, "uniOMls");
	GLint loc = glGetUniformLocation(prog, "omLsMap"), raw = glGetUniformLocation(prog, "omLsRaw");

	if (bi != GL_INVALID_INDEX)
	{
		glUniformBlockBinding(prog, bi, LS_BINDING);
	}

	if (loc != -1)
	{
		glUniform1i(loc, LS_UNIT);
	}

	if (raw != -1)
	{
		glUniform1i(raw, LS_RAWUNIT);
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

/* the cubes on their units for the world shaders; none while they are drawn (no texture bound while it is the
   target) */
static void
BindMap(qboolean on)
{
	GLuint t = (on && L.tex) ? L.tex : L.dummy;

	if (!LSEnoughUnits(L.maxunits))
	{
		return;
	}

	GL3_SelectTMU(GL_TEXTURE0 + LS_UNIT);
	glBindTexture(GL_TEXTURE_2D_ARRAY, t);
	GL3_SelectTMU(GL_TEXTURE0 + LS_RAWUNIT);
	glBindTexture(GL_TEXTURE_2D_ARRAY, (L.sampler && r_om_lshadow_search->value) ? t : L.dummy);
	GL3_SelectTMU(GL_TEXTURE0);
}

/* the sampler that reads depth on LS_RAWUNIT (no compare, nearest), 0 when the driver has none */
static void
CreateSampler(void)
{
	/* core in GL 3.3 and GLES 3.0, looked up from the context on every platform (before: Windows only, so Linux had
	   no penumbra blocker search) */
	L.sampler = 0;
	qglGenSamplers = (pfnGenSamplers)GL3_GetProcAddress("glGenSamplers");
	qglDeleteSamplers = (pfnDeleteSamplers)GL3_GetProcAddress("glDeleteSamplers");
	qglBindSampler = (pfnBindSampler)GL3_GetProcAddress("glBindSampler");
	qglSamplerParameteri = (pfnSamplerParameteri)GL3_GetProcAddress("glSamplerParameteri");

	if (!qglGenSamplers || !qglDeleteSamplers || !qglBindSampler || !qglSamplerParameteri)
	{
		return;
	}

	qglGenSamplers(1, &L.sampler);
	qglSamplerParameteri(L.sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	qglSamplerParameteri(L.sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	qglSamplerParameteri(L.sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	qglSamplerParameteri(L.sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	qglSamplerParameteri(L.sampler, GL_TEXTURE_COMPARE_MODE, GL_NONE);
	qglBindSampler(LS_RAWUNIT, L.sampler);
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
	r_om_lshadow_floor = ri.Cvar_Get("r_om_lshadow_floor", "0.4", CVAR_ARCHIVE);
	r_om_lshadow_lightsize = ri.Cvar_Get("r_om_lshadow_lightsize", "16", CVAR_ARCHIVE);
	r_om_lshadow_softmax = ri.Cvar_Get("r_om_lshadow_softmax", "24", CVAR_ARCHIVE);
	r_om_lshadow_taps = ri.Cvar_Get("r_om_lshadow_taps", "8", CVAR_ARCHIVE);
	r_om_lshadow_search = ri.Cvar_Get("r_om_lshadow_search", "1", CVAR_ARCHIVE);

	memset(&L.u, 0, sizeof(L.u));
	L.failed = L.active = L.capturing = false;
	L.tex = L.fbo = 0;
	L.size = L.nslots = 0;
	L.built = NULL;

	for (i = 0; i < LS_MAXSLOTS; i++)
	{
		L.slot[i] = -1;
		L.fade[i] = 0;
		L.share[i] = 1;
	}

	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &L.maxunits);
	ri.Cmd_AddCommand("r_om_lshadow_info", GL3_LShadow_Info);

	glGenBuffers(1, &L.ubo);
	UploadUBO();
	glBindBufferBase(GL_UNIFORM_BUFFER, LS_BINDING, L.ubo);
	L.uboenabled = false;

	if (LSEnoughUnits(L.maxunits))
	{
		glGenTextures(1, &L.dummy);
		GL3_SelectTMU(GL_TEXTURE0 + LS_UNIT);
		DepthArray(L.dummy, 1, LS_MAXSLOTS * 12);
		GL3_SelectTMU(GL_TEXTURE0);
		CreateSampler();
		BindMap(true);

		if (!L.sampler)
		{
			R_Printf(PRINT_ALL, "lamp shadows: no sampler objects, the penumbra is not searched\n");
		}
	}
	else
	{
		R_Printf(PRINT_ALL, "lamp shadows: %d texture units, need %d - off\n", L.maxunits, LS_RAWUNIT + 1);
		L.failed = true;
	}
}

void
GL3_LShadow_Shutdown(void)
{
	if (L.sampler && qglDeleteSamplers)
	{
		qglBindSampler(LS_RAWUNIT, 0);
		qglDeleteSamplers(1, &L.sampler);
	}

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
	const char *version = GL3_GLES_HEADER;
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

	return (d >= LS_FIXTURE) && (d < r);
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

/* a lamp's light at distance d in lightmap units (the compiler's falloff at the map's weights, no angles) */
static float
LampLight(const lslight_t *l, float d)
{
	if (l->kind == LS_SURF)
	{
		return l->intensity * 0.01f * wsurf / Q_max(d, 16.0f);
	}

	return Q_max(l->intensity - d * l->fade, 0.0f) * ((l->kind == LS_SPOT) ? wspot : wpoint);
}

/* a lamp's light on a face at p turned to n (lightmap units, its colour's brightest channel and style), as the
   world shader reckons it, 0 where the world hides the lamp */
static float
LampAt(const lslight_t *l, const vec3_t p, const vec3_t n)
{
	vec3_t d;
	float dist, ndl, c;

	VectorSubtract(l->o, p, d);
	dist = VectorLength(d);

	if ((dist >= l->radius) || (dist < 1))
	{
		return 0;
	}

	VectorScale(d, 1.0f / dist, d);
	ndl = DotProduct(n, d);

	if (ndl <= 0)
	{
		return 0;
	}

	c = LampLight(l, dist) * ndl;

	if (l->kind == LS_SURF)
	{
		c *= Q_max(-DotProduct(d, l->n), 0.0f);
	}
	else if (l->kind == LS_SPOT)
	{
		float ca = -DotProduct(d, l->dir);

		c = ((ca < l->stopdot) || (ca <= 0)) ? 0 : c * powf(ca, l->focus);
	}

	c *= Q_max(l->color[0], Q_max(l->color[1], l->color[2])) * StyleScale(l);

	if ((c > 0) && gl3_ghoul_trace)
	{
		vec3_t a;

		/* from just off the lamp toward p */
		a[0] = l->o[0] - d[0] * 4;
		a[1] = l->o[1] - d[1] * 4;
		a[2] = l->o[2] - d[2] * 4;

		if (gl3_ghoul_trace(a, p) < 0.97f)
		{
			return 0;
		}
	}

	return c;
}

/* slot j: its lamp's part of the light all the lamps give where its nearest caster stands (on the floor there).
   A shadow takes away no more than that: two lamps of a kind over a floor cast half shadows each */
static float
SlotShare(int j, const csmseg_t *segs, int nsegs)
{
	const lslight_t *l = &lights[L.slot[j]];
	static const vec3_t up = {0, 0, 1};
	vec3_t r, d;
	float best = 1e30f, mine, all = 0;
	int i, ni = -1;

	for (i = 0; i < nsegs; i++)
	{
		float sd = SegDist(&segs[i], l->o);

		if ((sd >= LS_FIXTURE) && (sd < l->radius) && (sd < best))
		{
			best = sd;
			ni = i;
		}
	}

	if (ni < 0)
	{
		return L.share[j];
	}

	r[0] = (segs[ni].mins[0] + segs[ni].maxs[0]) * 0.5f;
	r[1] = (segs[ni].mins[1] + segs[ni].maxs[1]) * 0.5f;
	r[2] = segs[ni].mins[2] + 2;
	VectorSubtract(r, L.shareref[j], d);

	/* measured again when the caster has moved a little */
	if ((DotProduct(d, d) < 12 * 12) && (L.share[j] < 1.5f))
	{
		return L.share[j];
	}

	VectorCopy(r, L.shareref[j]);
	mine = LampAt(l, r, up);

	if (mine <= 0)
	{
		/* the floor below the caster is out of this lamp: measure at its middle, facing the lamp */
		vec3_t n;

		r[2] = (segs[ni].mins[2] + segs[ni].maxs[2]) * 0.5f;
		VectorSubtract(l->o, r, n);
		VectorNormalize(n);

		for (i = 0; i < numlights; i++)
		{
			all += LampAt(&lights[i], r, n);
		}

		mine = LampAt(l, r, n);
	}
	else
	{
		for (i = 0; i < numlights; i++)
		{
			all += (&lights[i] == l) ? mine : LampAt(&lights[i], r, up);
		}
	}

	return (all > 0) ? Q_clamp(mine / all, 0.0f, 1.0f) : 1.0f;
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

			if ((sd >= LS_FIXTURE) && (sd < l->radius))
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

	/* slots: kept lamps stay where they are and fade in; a dropped lamp fades out and keeps its slot till then;
	   new ones take the free slots */
	{
		float dt = Q_clamp(r_newrefdef.time - L.lasttime, 0.0f, 0.1f);

		L.lasttime = r_newrefdef.time;

		for (j = 0; j < nslots; j++)
		{
			qboolean keep = false;

			if (L.slot[j] < 0)
			{
				continue;
			}

			for (k = 0; k < top; k++)
			{
				keep |= (pick[k] == L.slot[j]);
			}

			L.fade[j] = Q_clamp(L.fade[j] + (keep ? dt : -dt) / LS_FADETIME, 0.0f, 1.0f);

			if (!keep && (L.fade[j] <= 0))
			{
				L.slot[j] = -1;
			}
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
				L.fade[j] = 0;
				L.share[j] = 2;   /* measured at once */
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
		/* lightmap units to the shader's 0..1 */
		sc = StyleScale(l) / 255.0f;
		top = j + 1;
		L.share[j] = SlotShare(j, segs, nsegs);

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
			/* omlight's inverse-linear patch light (intensity x cos x cos / d x 0.01) at its fitted weight */
			L.u.a[j][0] = l->n[0];
			L.u.a[j][1] = l->n[1];
			L.u.a[j][2] = l->n[2];
			L.u.a[j][3] = l->intensity * 0.01f * wsurf;
			L.u.soft[j][0] = l->size;
		}
		else
		{
			float w = (l->kind == LS_SPOT) ? wspot : wpoint;

			/* the linear falloff at its fitted weight: (intensity - d x fade) x w */
			L.u.a[j][0] = l->intensity * w;
			L.u.a[j][1] = l->fade * w;
			L.u.a[j][2] = l->focus;
			L.u.a[j][3] = l->stopdot;
			L.u.dir[j][0] = l->dir[0];
			L.u.dir[j][1] = l->dir[1];
			L.u.dir[j][2] = l->dir[2];
			L.u.soft[j][0] = Q_clamp(r_om_lshadow_lightsize->value, 1.0f, 64.0f);
		}

		L.u.soft[j][1] = L.fade[j];
		L.u.soft[j][3] = (float)Q_clamp(((int)r_om_lshadow_taps->value + 3) & ~3, 4, 16);   /* filter taps (a blocker search of half) */
		L.u.soft[j][2] = L.share[j];

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
	/* the lightmap holds the bounced and ambient light too, which a caster does not block: a shadow keeps
	   r_om_lshadow_floor of the light (0.2 left a bin under its wall lamp black) */
	L.u.params[2] = Q_clamp(r_om_lshadow_floor->value, 0.0f, 1.0f);
	L.u.params[3] = 2.0f * LS_TAN / size;
	L.u.params2[0] = 1.0f / size;
	L.u.params2[1] = r_om_lshadow_debug->value ? 1.0f : 0.0f;
	L.u.params2[2] = (L.sampler && r_om_lshadow_search->value) ? 1.0f : 0.0f;
	L.u.params2[3] = Q_clamp(r_om_lshadow_softmax->value, 1.0f, 64.0f) / size;
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

	BindMap(false);
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

	BindMap(true);
	L.active = true;
}

/* lamp shadows are on this frame: the light beams' own character shadows in their pools give way (gl3_post.c) */
qboolean
GL3_LShadow_On(void)
{
	return L.capturing;
}

/* how much of a character's ground blob is left: none where a lamp with a shadow cube lights it well (its own
   shadow grounds it), faded over a few lightmap units so a lamp taking or leaving a slot does not pop */
float
GL3_LShadow_BlobKeep(const vec3_t origin)
{
	float best = 0;
	int j;

	if (!L.active)
	{
		return 1;
	}

	for (j = 0; j < LS_MAXSLOTS; j++)
	{
		const lslight_t *l;
		vec3_t d;
		float dist, c;

		if (L.slot[j] < 0)
		{
			continue;
		}

		l = &lights[L.slot[j]];
		VectorSubtract(origin, l->o, d);
		dist = VectorLength(d);

		if ((dist >= l->radius) || (dist < 1))
		{
			continue;
		}

		VectorScale(d, 1.0f / dist, d);

		if ((l->kind == LS_SPOT) && (DotProduct(d, l->dir) < l->stopdot))
		{
			continue;
		}

		if ((l->kind == LS_SURF) && (DotProduct(d, l->n) <= 0))
		{
			continue;
		}

		/* a faint shadow (a lamp among many) leaves part of the blob */
		c = LampLight(l, dist) * StyleScale(l);
		best = Q_max(best, Q_clamp((c - 4.0f) / 12.0f, 0.0f, 1.0f) * L.fade[j] * Q_clamp(L.share[j] * 2.0f, 0.0f, 1.0f));
	}

	/* half stays: the blob is the bounced light the body keeps from the floor around its feet, no lamp's shadow -
	   unless r_om_ssao darkens the feet itself (the owner: the old must go where the new does its job) */
	return 1.0f - (GL3_Post_SSAOActive() ? 1.0f : 0.5f) * best;
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
	R_Printf(PRINT_ALL, "lamp shadows: texture units %d / %d of %d\n", LS_UNIT, LS_RAWUNIT, L.maxunits);

	for (j = 0; j < LS_MAXSLOTS; j++)
	{
		const lslight_t *l;

		if (L.slot[j] < 0)
		{
			continue;
		}

		l = &lights[L.slot[j]];
		R_Printf(PRINT_ALL, "  slot %d: %s %d at (%.0f %.0f %.0f) intensity %g radius %.0f style %d (%.2f) "
			"share %.2f fade %.2f\n", j, kinds[l->kind], L.slot[j], l->o[0], l->o[1], l->o[2], l->intensity, l->radius,
			l->style, StyleScale(l), L.share[j], L.fade[j]);
		n++;
	}

	if (!n)
	{
		R_Printf(PRINT_ALL, "  no lamp has a moving caster in reach\n");
	}
}
