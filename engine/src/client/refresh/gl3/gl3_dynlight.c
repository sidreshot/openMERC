/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: dynamic lights on the lightmaps of Soldier of
 * Fortune maps by the Quake II per-sample rule with SoF's constants. For every
 * lit face marked by a dynamic light this frame, the light added to each
 * lightmap sample is computed on the CPU and stored in a per-page "dynamic"
 * layer (page size, float RGBA, texture unit 6) that the lightmapped shaders add
 * to the static light sum before rounding, scaling and the colour table, as the
 * original adds it to the samples it rebuilds. Blocks lit in the previous frame
 * are cleared at the start of the next one. Dynamic light is not multiplied by
 * gl_modulate. Quake II maps keep Yamagi's per-pixel lights (the layer stays 0).
 *
 * Implements: SPEC-410 (Lightmaps: dynamic lights on lightmaps, gl_dynamic)
 */

#include "header/local.h"

#define DYN_MAX_SAMPLES 12288 /* SPEC-410: at most 12 288 samples per face */

static GLuint dyntex[MAX_LIGHTMAPS];  /* 0 = page without dynamic light so far */
static GLuint zerotex;                /* bound for pages without a layer */
static gl3model_t *dynworld;          /* the world the layers belong to */

/* faces whose block holds dynamic light (cleared at the next frame) */
static msurface_t **litfaces;
static int numlit, maxlit;
static int *litframe;                 /* per world surface: the frame its block was computed */
static int numlitframe;

static float blockbuf[DYN_MAX_SAMPLES * 4];

static void
CreateLayer(GLuint *tex, int w, int h)
{
	float *zeros = calloc((size_t)w * h * 4, sizeof(float));

	glGenTextures(1, tex);
	GL3_SelectTMU(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, *tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, zeros);
	free(zeros);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/* frees all layers (new world, renderer shutdown) */
void
GL3_DynLight_Free(void)
{
	int i;

	for (i = 0; i < MAX_LIGHTMAPS; i++)
	{
		if (dyntex[i])
		{
			glDeleteTextures(1, &dyntex[i]);
			dyntex[i] = 0;
		}
	}

	if (zerotex)
	{
		glDeleteTextures(1, &zerotex);
		zerotex = 0;
	}

	free(litfaces);
	litfaces = NULL;
	numlit = maxlit = 0;
	free(litframe);
	litframe = NULL;
	numlitframe = 0;
	dynworld = NULL;
}

/* binds the dynamic layer of a lightmap page to texture unit 6 (called with the page's lightmaps) */
void
GL3_DynLight_BindPage(int page)
{
	if (!zerotex)
	{
		CreateLayer(&zerotex, 1, 1);
	}

	GL3_SelectTMU(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, ((page >= 0) && (page < MAX_LIGHTMAPS) && dyntex[page]) ? dyntex[page] : zerotex);
}

static void
UploadBlock(msurface_t *surf, const float *data)
{
	int page = surf->lightmaptexturenum;

	if (!dyntex[page])
	{
		CreateLayer(&dyntex[page], BLOCK_WIDTH, BLOCK_HEIGHT); /* the size of a lightmap page */
	}

	GL3_SelectTMU(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, dyntex[page]);
	glTexSubImage2D(GL_TEXTURE_2D, 0, surf->light_s, surf->light_t, surf->lmsize[0], surf->lmsize[1], GL_RGBA,
		GL_FLOAT, data);

	/* the page bound for drawing may be another one: let the next GL3_BindLightmap rebind */
	gl3state.currentlightmap = -1;
}

/* start of a 3D frame: blocks lit in the previous frame go back to 0 */
void
GL3_DynLight_BeginFrame(void)
{
	int i;

	if (dynworld != gl3_worldmodel)
	{
		GL3_DynLight_Free();
		dynworld = gl3_worldmodel;
	}

	if (!numlit)
	{
		return;
	}

	memset(blockbuf, 0, sizeof(blockbuf));

	for (i = 0; i < numlit; i++)
	{
		UploadBlock(litfaces[i], blockbuf);
	}

	numlit = 0;
}

static qboolean
Remember(msurface_t *surf)
{
	int idx;

	if (!gl3_worldmodel || !gl3_worldmodel->surfaces)
	{
		return false;
	}

	idx = (int)(surf - gl3_worldmodel->surfaces);

	if ((idx < 0) || (idx >= gl3_worldmodel->numsurfaces))
	{
		return false;
	}

	if (numlitframe < gl3_worldmodel->numsurfaces)
	{
		int *n = realloc(litframe, gl3_worldmodel->numsurfaces * sizeof(int));

		if (!n)
		{
			return false;
		}

		memset(n + numlitframe, 0xff, (gl3_worldmodel->numsurfaces - numlitframe) * sizeof(int));
		litframe = n;
		numlitframe = gl3_worldmodel->numsurfaces;
	}

	if (litframe[idx] == gl3_framecount)
	{
		return false; /* already done this frame */
	}

	if (numlit == maxlit)
	{
		int m = maxlit ? maxlit * 2 : 256;
		msurface_t **n = realloc(litfaces, m * sizeof(*n));

		if (!n)
		{
			return false;
		}

		litfaces = n;
		maxlit = m;
	}

	litframe[idx] = gl3_framecount;
	litfaces[numlit++] = surf;
	return true;
}

/*
 * The dynamic light of a face marked this frame, by the rule of SPEC-410
 * "Dynamic lights on lightmaps": d = signed distance from the light to the
 * face plane, r = intensity - |d|, nothing if r < 64; (ls, lt) = the light's
 * projection on the plane in texture units relative to lm_mins; sample (i, j)
 * at (16 lm_scale_s i, 16 lm_scale_t j); ds, dt = integer distances;
 * dist = max(ds, dt) + min(ds, dt) / 2; if dist < r - 64 the sample gains
 * (r - dist) x colour.
 */
void
GL3_DynLight_Face(msurface_t *surf)
{
	const mtexinfo_t *tex = surf->texinfo;
	int w = surf->lmsize[0], h = surf->lmsize[1];
	int lnum, i, j;
	qboolean any = false;
	dlight_t *dl;

	if ((surf->dlightframe != gl3_framecount) || !surf->dlightbits)
	{
		return;
	}

	if (gl_dynamic && !gl_dynamic->value)
	{
		return; /* SPEC-410 World diagnostics: lightmaps are not updated after loading */
	}

	if ((w <= 0) || (h <= 0) || (w * h > DYN_MAX_SAMPLES) || !surf->samples ||
		(surf->texinfo->flags & (SOFSURF_SKY | SURF_WARP | SOFSURF_ALPHA)))
	{
		return;
	}

	memset(blockbuf, 0, (size_t)w * h * 4 * sizeof(float));

	for (lnum = 0, dl = r_newrefdef.dlights; lnum < r_newrefdef.num_dlights; lnum++, dl++)
	{
		vec3_t impact;
		float d, r, ls, lt;

		if (!(surf->dlightbits & (1u << lnum)))
		{
			continue;
		}

		d = DotProduct(dl->origin, surf->plane->normal) - surf->plane->dist;
		r = dl->intensity - fabsf(d);

		if (r < DLIGHT_CUTOFF)
		{
			continue;
		}

		VectorMA(dl->origin, -d, surf->plane->normal, impact);
		ls = DotProduct(impact, tex->vecs[0]) + tex->vecs[0][3] - surf->texturemins[0];
		lt = DotProduct(impact, tex->vecs[1]) + tex->vecs[1][3] - surf->texturemins[1];

		for (j = 0; j < h; j++)
		{
			int dt = (int)(lt - (float)(surf->lmstep[1] * j));

			if (dt < 0)
			{
				dt = -dt;
			}

			for (i = 0; i < w; i++)
			{
				int ds = (int)(ls - (float)(surf->lmstep[0] * i));
				int dist;
				float *o;

				if (ds < 0)
				{
					ds = -ds;
				}

				dist = (ds > dt) ? ds + (dt >> 1) : dt + (ds >> 1);

				if ((float)dist >= r - DLIGHT_CUTOFF)
				{
					continue;
				}

				/* sample units 0..255 are 0..1 in the shader */
				o = &blockbuf[(j * w + i) * 4];
				o[0] += (r - dist) * dl->color[0] * (1.0f / 255.0f);
				o[1] += (r - dist) * dl->color[1] * (1.0f / 255.0f);
				o[2] += (r - dist) * dl->color[2] * (1.0f / 255.0f);
				any = true;
			}
		}
	}

	if (!any || !Remember(surf))
	{
		return;
	}

	UploadBlock(surf, blockbuf);
}

/* ------------------------------------------------------------------
 * GFX-modern r_om_pdlight (docs/impl/GFX-modern.md §8): the same rule per
 * pixel in the lightmapped shaders (omSoFDlights in gl3_shaders.c). The
 * lights are the frame's uniLights (GL3_PushDlights); a face gets its
 * dlight bits as vertex light flags instead of a CPU-filled layer.
 * ------------------------------------------------------------------ */

static cvar_t *r_om_pdlight, *r_om_pdlight_max, *r_om_pdlight_scale, *r_om_pdlight_normal;
static qboolean pdlview;

static void
PdlCvars(void)
{
	if (!r_om_pdlight)
	{
		r_om_pdlight = ri.Cvar_Get("r_om_pdlight", "1", CVAR_ARCHIVE);
		r_om_pdlight_max = ri.Cvar_Get("r_om_pdlight_max", "32", CVAR_ARCHIVE);
		r_om_pdlight_scale = ri.Cvar_Get("r_om_pdlight_scale", "1", CVAR_ARCHIVE);
		r_om_pdlight_normal = ri.Cvar_Get("r_om_pdlight_normal", "0", CVAR_ARCHIVE);
	}
}

/* once per 3D view (SetupGL): enabled for SoF worlds with gl_dynamic on */
void
GL3_DynLight_SetupView(gl3Uni3D_t *u)
{
	PdlCvars();

	pdlview = (r_om_pdlight->value != 0) && gl3_worldmodel && (gl3_worldmodel->bspversion == BSPVERSION_SOF) &&
		!(r_newrefdef.rdflags & RDF_NOWORLDMODEL) && (!gl_dynamic || gl_dynamic->value);

	u->omPdl.X = pdlview ? 1.0f : 0.0f;
	u->omPdl.Y = r_om_pdlight_scale->value;
	u->omPdl.Z = Q_max(0.0f, Q_min(r_om_pdlight_normal->value, 1.0f));
	u->omPdl.W = 0.0f;
	u->omTexDim.X = u->omTexDim.Y = 0.0f;
}

qboolean
GL3_DynLight_PerPixel(void)
{
	return pdlview;
}

/* the vertex light flags of a face drawn this view (same exclusions as the sample layer) */
unsigned
GL3_DynLight_PixelFlags(msurface_t *surf)
{
	unsigned bits;
	int max;

	if (!pdlview || (surf->dlightframe != gl3_framecount) || !surf->dlightbits || !surf->samples ||
		(surf->texinfo->flags & (SOFSURF_SKY | SURF_WARP | SOFSURF_ALPHA)))
	{
		return 0;
	}

	bits = surf->dlightbits;
	max = (int)r_om_pdlight_max->value;

	if ((max >= 0) && (max < 32))
	{
		bits &= (1u << max) - 1u;
	}

	return bits;
}

/*
 * r_om_pdlight_test "intensity distance [r g b]" (measurement only, not archived): one extra
 * dynamic light on the view axis, distance units in front of the eye, for the A/B of the grid
 * layer against r_om_pdlight 1 (test/gfx).
 */
/* r_om_dlight_stats 1 (diagnostic, not archived): the dynamic lights the renderer gets, twice a second */
static void
DlightStats(const refdef_t *rd)
{
	static cvar_t *st;
	static int last;
	int i, now;

	if (!st)
	{
		st = ri.Cvar_Get("r_om_dlight_stats", "0", 0);
	}

	if (!st->value)
	{
		return;
	}

	now = (int)(rd->time * 1000.0f);

	if ((rd->num_dlights == 0) && (now - last < 2000) && (now >= last))
	{
		return;
	}

	if ((now - last < 500) && (now >= last))
	{
		return;
	}

	last = now;
	R_Printf(PRINT_ALL, "OMDL t %.2f n %d gl_dynamic %g gl_dlightintensity %g r_om_pdlight %g:", rd->time, rd->num_dlights,
		gl_dynamic ? gl_dynamic->value : -1.0f, gl_dlightintensity ? gl_dlightintensity->value : -1.0f,
		r_om_pdlight ? r_om_pdlight->value : -1.0f);

	for (i = 0; (i < rd->num_dlights) && (i < 6); i++)
	{
		R_Printf(PRINT_ALL, " [%.0f %.0f %.0f i %.0f c %.2f %.2f %.2f]", rd->dlights[i].origin[0], rd->dlights[i].origin[1],
			rd->dlights[i].origin[2], rd->dlights[i].intensity, rd->dlights[i].color[0], rd->dlights[i].color[1],
			rd->dlights[i].color[2]);
	}

	R_Printf(PRINT_ALL, "\n");
}

void
GL3_DynLight_TestLight(refdef_t *rd)
{
	DlightStats(rd);

	static cvar_t *r_om_pdlight_test;
	static dlight_t lights[MAX_DLIGHTS];
	float v[5] = {0, 64, 1, 1, 1};
	vec3_t fwd, right, up;
	int n;

	if (!r_om_pdlight_test)
	{
		r_om_pdlight_test = ri.Cvar_Get("r_om_pdlight_test", "0", 0);
	}

	if (!r_om_pdlight_test->string[0] || (r_om_pdlight_test->string[0] == '0') ||
		(rd->rdflags & RDF_NOWORLDMODEL) || (rd->num_dlights >= MAX_DLIGHTS))
	{
		return;
	}

	sscanf(r_om_pdlight_test->string, "%f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4]);
	n = rd->num_dlights;

	if (n > 0)
	{
		memcpy(lights, rd->dlights, n * sizeof(dlight_t));
	}

	memset(&lights[n], 0, sizeof(dlight_t));
	AngleVectors(rd->viewangles, fwd, right, up);
	VectorMA(rd->vieworg, v[1], fwd, lights[n].origin);
	lights[n].intensity = v[0];
	lights[n].color[0] = v[2];
	lights[n].color[1] = v[3];
	lights[n].color[2] = v[4];
	rd->dlights = lights;
	rd->num_dlights = n + 1;
}
