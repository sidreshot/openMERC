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
 * Lightmap handling
 *
 * =======================================================================
 *
 * OpenMerc: lightmap size and sample spacing come from msurface_t
 * (per-face scale of IBSP v46). Package I4c: gl_monolightmap latched at
 * map load (applied in the lightmapped shaders).
 * Implements: SPEC-101, SPEC-410
 */

#include "header/local.h"


#define TEXNUM_LIGHTMAPS 1024

extern gl3lightmapstate_t gl3_lms;

/* OpenMerc (I4c): gl_monolightmap latched when the world's lightmaps are built */
int gl3_monolightmap_mode;

void
GL3_LM_InitBlock(void)
{
	memset(gl3_lms.allocated, 0, sizeof(gl3_lms.allocated));
}

/* GFX-modern r_om_deluxe: the light directions of the re-bake in an atlas of the same layout as the lightmap pages
   (style 0), RGBA8 (128 = 0; alpha 255 where a face has directions), bound on GL_TEXTURE10 with the page */
static byte deluxebuf[4 * BLOCK_WIDTH * BLOCK_HEIGHT];
static GLuint deluxetex[MAX_LIGHTMAPS];
static int deluxegen[MAX_LIGHTMAPS];
static qboolean deluxemap;     /* the map being built has directions */
static qboolean deluxeany;     /* ... and at least one face got them */
static gl3model_t *deluxemodel;

qboolean
GL3_LM_DeluxeOn(void)
{
	return deluxeany;
}

static void
DeluxeClear(void)
{
	int i;

	for (i = 0; i < BLOCK_WIDTH * BLOCK_HEIGHT; i++)
	{
		deluxebuf[i * 4 + 0] = deluxebuf[i * 4 + 1] = deluxebuf[i * 4 + 2] = 128;
		deluxebuf[i * 4 + 3] = 0;
	}
}

/* the directions of a face's style-0 samples into the page buffer at its place */
static void
DeluxeFill(const msurface_t *surf)
{
	int smax = surf->lmsize[0], tmax = surf->lmsize[1], i, j;
	const byte *src;

	if (!deluxemap || !surf->samples || !deluxemodel || !deluxemodel->lightdata)
	{
		return;
	}

	src = GL3_Relight_Deluxe((int)(surf->samples - deluxemodel->lightdata), smax * tmax * 3);

	if (!src)
	{
		return;
	}

	for (i = 0; i < tmax; i++)
	{
		byte *dst = deluxebuf + ((surf->light_t + i) * BLOCK_WIDTH + surf->light_s) * 4;

		for (j = 0; j < smax; j++, dst += 4, src += 3)
		{
			dst[0] = src[0];
			dst[1] = src[1];
			dst[2] = src[2];
			dst[3] = 255;
		}
	}

	deluxeany = true;
}

/* binds the page's directions (GL_TEXTURE10), with the lightmaps */
void
GL3_LM_BindDeluxe(int page)
{
	if (!deluxeany || (page < 0) || (page >= MAX_LIGHTMAPS) || !deluxetex[page] || (deluxegen[page] != gl3_contextgen))
	{
		return;
	}

	GL3_SelectTMU(GL_TEXTURE10);
	glBindTexture(GL_TEXTURE_2D, deluxetex[page]);
}

void
GL3_LM_UploadBlock(void)
{
	int map;

	if (deluxemap)
	{
		int page = gl3_lms.current_lightmap_texture;

		if (!deluxetex[page] || (deluxegen[page] != gl3_contextgen))
		{
			glGenTextures(1, &deluxetex[page]);
			deluxegen[page] = gl3_contextgen;
		}

		GL3_SelectTMU(GL_TEXTURE10);
		glBindTexture(GL_TEXTURE_2D, deluxetex[page]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, BLOCK_WIDTH, BLOCK_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, deluxebuf);
		DeluxeClear();
	}

	// NOTE: we don't use the dynamic lightmap anymore - all lightmaps are loaded at level load
	//       and not changed after that. they're blended dynamically depending on light styles
	//       though, and dynamic lights are (will be) applied in shader, hopefully per fragment.

	GL3_BindLightmap(gl3_lms.current_lightmap_texture);

	// upload all 4 lightmaps
	for(map=0; map < MAX_LIGHTMAPS_PER_SURFACE; ++map)
	{
		GL3_SelectTMU(GL_TEXTURE1+map); // this relies on GL_TEXTURE2 being GL_TEXTURE1+1 etc
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_LIGHTMAP_FORMAT,
		             BLOCK_WIDTH, BLOCK_HEIGHT, 0, GL_LIGHTMAP_FORMAT,
		             GL_UNSIGNED_BYTE, gl3_lms.lightmap_buffers[map]);
	}

	if (++gl3_lms.current_lightmap_texture == MAX_LIGHTMAPS)
	{
		Com_Error(ERR_DROP, "LM_UploadBlock() - MAX_LIGHTMAPS exceeded\n");
	}
}

/*
 * returns a texture number and the position inside it
 */
qboolean
GL3_LM_AllocBlock(int w, int h, int *x, int *y)
{
	/* OpenMerc: the Quake II skyline search with the maximum of each window of w columns from a monotonic queue -
	   O(BLOCK_WIDTH) instead of O(BLOCK_WIDTH x w) per lightmap. A spot is valid when its window's maximum is below the
	   best height so far, and then becomes the best: the same spots in the same order as the column-by-column test */
	static int queue[BLOCK_WIDTH];
	int i, best, head = 0, tail = 0;

	best = BLOCK_HEIGHT;

	if ((w <= 0) || (w >= BLOCK_WIDTH))
	{
		return false;
	}

	for (i = 0; i < BLOCK_WIDTH - 1; i++)
	{
		int start = i - w + 1;

		/* column i enters the window, smaller or equal ones before it can never be its maximum again */
		while ((tail > head) && (gl3_lms.allocated[queue[tail - 1]] <= gl3_lms.allocated[i]))
		{
			tail--;
		}

		queue[tail++] = i;

		if (queue[head] < start)
		{
			head++;
		}

		/* the window start .. i is the spot at column start (start < BLOCK_WIDTH - w, as before) */
		if ((start >= 0) && (gl3_lms.allocated[queue[head]] < best))
		{
			*x = start;
			*y = best = gl3_lms.allocated[queue[head]];
		}
	}

	if (best + h > BLOCK_HEIGHT)
	{
		return false;
	}

	for (i = 0; i < w; i++)
	{
		gl3_lms.allocated[*x + i] = best + h;
	}

	return true;
}

void
GL3_LM_BuildPolygonFromSurface(gl3model_t *currentmodel, msurface_t *fa)
{
	int i, lindex, lnumverts;
	medge_t *pedges, *r_pedge;
	float *vec;
	float s, t;
	glpoly_t *poly;
	vec3_t total;
	vec3_t normal;

	/* reconstruct the polygon */
	pedges = currentmodel->edges;
	lnumverts = fa->numedges;

	VectorClear(total);

	/* draw texture */
	poly = Hunk_Alloc(sizeof(glpoly_t) +
		   (lnumverts - 4) * sizeof(gl3_3D_vtx_t));
	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	VectorCopy(fa->plane->normal, normal);

	if(fa->flags & SURF_PLANEBACK)
	{
		// if for some reason the normal sticks to the back of the plane, invert it
		// so it's usable for the shader
		for (i=0; i<3; ++i)  normal[i] = -normal[i];
	}

	for (i = 0; i < lnumverts; i++)
	{
		gl3_3D_vtx_t* vert = &poly->vertices[i];

		lindex = currentmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			vec = currentmodel->vertexes[r_pedge->v[0]].position;
		}
		else
		{
			r_pedge = &pedges[-lindex];
			vec = currentmodel->vertexes[r_pedge->v[1]].position;
		}

		s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= fa->texinfo->image->width;

		t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= fa->texinfo->image->height;

		VectorAdd(total, vec, total);
		VectorCopy(vec, vert->pos);
		vert->texCoord[0] = s;
		vert->texCoord[1] = t;

		/* lightmap texture coordinates: sample i lies at
		   texturemins + lmstep * i (16 texels in Quake II, 16 or 32 in
		   SoF v46, SPEC-101), at the center of lightmap texel i */
		s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= fa->texturemins[0];
		s /= fa->lmstep[0];
		/* OpenMerc (I4c), SPEC-410 Texture coordinates: with a sample spacing other than 16 the
		   sample position is floored */
		if (fa->lmstep[0] != 16)
		{
			s = floorf(s);
		}
		s += fa->light_s + 0.5f;
		s /= BLOCK_WIDTH;

		t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= fa->texturemins[1];
		t /= fa->lmstep[1];
		if (fa->lmstep[1] != 16)
		{
			t = floorf(t);
		}
		t += fa->light_t + 0.5f;
		t /= BLOCK_HEIGHT;

		vert->lmTexCoord[0] = s;
		vert->lmTexCoord[1] = t;

		VectorCopy(normal, vert->normal);
		vert->lightFlags = 0;
	}
}

void
GL3_LM_CreateSurfaceLightmap(msurface_t *surf)
{
	int smax, tmax;

	if (surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB))
	{
		return;
	}

	smax = surf->lmsize[0];
	tmax = surf->lmsize[1];

	if (!GL3_LM_AllocBlock(smax, tmax, &surf->light_s, &surf->light_t))
	{
		GL3_LM_UploadBlock();
		GL3_LM_InitBlock();

		if (!GL3_LM_AllocBlock(smax, tmax, &surf->light_s, &surf->light_t))
		{
			Com_Error(ERR_FATAL, "Consecutive calls to LM_AllocBlock(%d,%d) failed\n",
					smax, tmax);
		}
	}

	surf->lightmaptexturenum = gl3_lms.current_lightmap_texture;

	GL3_BuildLightMap(surf, (surf->light_t * BLOCK_WIDTH + surf->light_s) * LIGHTMAP_BYTES, BLOCK_WIDTH * LIGHTMAP_BYTES);
	DeluxeFill(surf); /* GFX-modern r_om_deluxe */
}

void
GL3_LM_BeginBuildingLightmaps(gl3model_t *m)
{
	static lightstyle_t lightstyles[MAX_LIGHTSTYLES];
	int i;

	GL3_DynLight_Free(); /* OpenMerc (I4c): new lightmap pages, the dynamic layers start again */
	GL3_World_Invalidate(); /* gl3_main.c r_om_worldvbo: new polygons */

	memset(gl3_lms.allocated, 0, sizeof(gl3_lms.allocated));

	gl3_framecount = 1; /* no dlightcache */

	/* setup the base lightstyles so the lightmaps
	   won't have to be regenerated the first time
	   they're seen */
	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		lightstyles[i].rgb[0] = 1;
		lightstyles[i].rgb[1] = 1;
		lightstyles[i].rgb[2] = 1;
		lightstyles[i].white = 3;
	}

	r_newrefdef.lightstyles = lightstyles;

	gl3_lms.current_lightmap_texture = 0;

	/* GFX-modern r_om_deluxe: the re-bake's directions, PC only (the highlight needs the reflection material) */
#ifdef YQ2_GL3_GLES3
	deluxemap = false;
#else
	deluxemap = m && m->lightdata && (GL3_Relight_Deluxe(0, 1) != NULL);
#endif
	deluxeany = false;
	deluxemodel = m;
	DeluxeClear();

	/* OpenMerc (I4c): the texel and upload format of SoF lightmaps follow the first character of
	   gl_monolightmap when the lightmaps are built (SPEC-410 Lightmaps): 0 colour table, L, I, C, A, other */
	switch ((gl_monolightmap && gl_monolightmap->string) ? gl_monolightmap->string[0] : '0')
	{
		case '0': gl3_monolightmap_mode = 0; break;
		case 'L': gl3_monolightmap_mode = 1; break;
		case 'I': gl3_monolightmap_mode = 2; break;
		case 'C': gl3_monolightmap_mode = 3; break;
		case 'A': gl3_monolightmap_mode = 4; break;
		default: gl3_monolightmap_mode = 5; break;
	}

	// Note: the dynamic lightmap used to be initialized here, we don't use that anymore.
}

void
GL3_LM_EndBuildingLightmaps(void)
{
	GL3_LM_UploadBlock();
	R_Printf(PRINT_DEVELOPER, "r_om_deluxe: directions %s\n", deluxeany ? "on" : (deluxemap ? "in the cache, no face" : "none"));
}

/*
 * OpenMerc GFX-modern r_om_lmstitch: lightmap seam stitching. Every BSP face has a lightmap of its own; where a
 * plane is cut into several faces (a shop shutter split by the planes of the brushes around it) the compiler lit
 * each face on its own grid and the two lightmaps disagree along the shared border: a straight step of light
 * across a wall. As the light bakers do it (Unity / Bakery "stitch seams", Sylvan's lightmap seam stitching): along
 * every border of two coplanar faces, every half sample, both lightmaps are read bilinearly as drawn; the samples
 * that make up each reading are moved toward the mean of the two (three rounds), and the correction fades into the
 * face over r_om_lmstitch_width samples, so no new step appears one sample in. Per light style (the same style on
 * both sides); before the lightmaps are uploaded, so everything that reads them (models' light point) agrees.
 */

typedef struct
{
	const cplane_t *plane;
	int back, face;
	vec3_t a, b;
	vec3_t mn, mx;   /* bounds of the edge (a quick reject of pairs that cannot share a stretch) */
} lmedge_t;

/* a border stretch shared by edges i and j (the geometry does not change between the rounds) */
typedef struct
{
	int i, j;
	float lo, hi;
} lmpair_t;

static int
LMEdgeCmp(const void *x, const void *y)
{
	const lmedge_t *p = x, *q = y;

	if (p->plane != q->plane)
	{
		return (p->plane < q->plane) ? -1 : 1;
	}

	return p->back - q->back;
}

static qboolean
LMStitchFace(const msurface_t *s)
{
	return s->samples && s->texinfo && (s->styles[0] != 255) &&
		!(s->texinfo->flags & (SURF_SKY | SURF_WARP | SURF_TRANS33 | SURF_TRANS66 | SURF_NODRAW | SOFSURF_ALPHA));
}

/* bilinear position in the face's samples: the 4 samples and weights at p */
static void
LMTaps(const msurface_t *s, const vec3_t p, int idx[4], float w[4])
{
	float u = (DotProduct(p, s->texinfo->vecs[0]) + s->texinfo->vecs[0][3] - s->texturemins[0]) / s->lmstep[0];
	float v = (DotProduct(p, s->texinfo->vecs[1]) + s->texinfo->vecs[1][3] - s->texturemins[1]) / s->lmstep[1];
	int x0, y0, x1, y1;
	float fx, fy;

	u = Q_clamp(u, 0.0f, (float)(s->lmsize[0] - 1));
	v = Q_clamp(v, 0.0f, (float)(s->lmsize[1] - 1));
	x0 = (int)u;
	y0 = (int)v;
	x1 = Q_min(x0 + 1, s->lmsize[0] - 1);
	y1 = Q_min(y0 + 1, s->lmsize[1] - 1);
	fx = u - x0;
	fy = v - y0;
	idx[0] = y0 * s->lmsize[0] + x0;
	idx[1] = y0 * s->lmsize[0] + x1;
	idx[2] = y1 * s->lmsize[0] + x0;
	idx[3] = y1 * s->lmsize[0] + x1;
	w[0] = (1 - fx) * (1 - fy);
	w[1] = fx * (1 - fy);
	w[2] = (1 - fx) * fy;
	w[3] = fx * fy;
}

/* the border stretch edges i < j of one plane share, along i: false when they do not share 2 units */
static qboolean
LMPairTest(const lmedge_t *ed, int i, int j, float *plo, float *phi)
{
	vec3_t t, r0, r1, off;
	float len, s0, s1, lo, hi;

	if (ed[i].face == ed[j].face)
	{
		return false;
	}

	/* a stretch of 2 units with both ends of j within 0.1 of i's line lies in both bounds */
	if ((ed[j].mn[0] > ed[i].mx[0] + 0.25f) || (ed[j].mx[0] < ed[i].mn[0] - 0.25f) ||
		(ed[j].mn[1] > ed[i].mx[1] + 0.25f) || (ed[j].mx[1] < ed[i].mn[1] - 0.25f) ||
		(ed[j].mn[2] > ed[i].mx[2] + 0.25f) || (ed[j].mx[2] < ed[i].mn[2] - 0.25f))
	{
		return false;
	}

	VectorSubtract(ed[i].b, ed[i].a, t);
	len = VectorNormalize(t);
	VectorSubtract(ed[j].a, ed[i].a, r0);
	VectorSubtract(ed[j].b, ed[i].a, r1);
	s0 = DotProduct(r0, t);
	s1 = DotProduct(r1, t);
	VectorMA(r0, -s0, t, off);

	if (DotProduct(off, off) > 0.01f)
	{
		return false;
	}

	VectorMA(r1, -s1, t, off);

	if (DotProduct(off, off) > 0.01f)
	{
		return false;
	}

	lo = Q_max(0.0f, Q_min(s0, s1));
	hi = Q_min(len, Q_max(s0, s1));

	if (hi - lo < 2.0f)
	{
		return false;
	}

	*plo = lo;
	*phi = hi;
	return true;
}

/* the edges of a plane group by their low bound along the sweep axis */
static const lmedge_t *lmsweep_ed;
static int lmsweep_axis;

static int
LMSweepCmp(const void *x, const void *y)
{
	float a = lmsweep_ed[*(const int *)x].mn[lmsweep_axis], b = lmsweep_ed[*(const int *)y].mn[lmsweep_axis];

	return (a < b) ? -1 : (a > b) ? 1 : (*(const int *)x - *(const int *)y);
}

static int
LMPairCmp(const void *x, const void *y)
{
	const lmpair_t *p = x, *q = y;

	return (p->i != q->i) ? p->i - q->i : p->j - q->j;
}

/* every shared stretch of the sorted edges, in the order of the pairs (i, j > i) of each plane group. OpenMerc: a
   sweep along the group's longest axis (only the edges whose bounds overlap there are tested; a floor plane holds
   thousands of edges, all pairs took most of the stitch time), then the pairs sorted back into that order, so the
   rounds accumulate in the same order as before. */
static int
LMFindPairs(const lmedge_t *ed, int numed, lmpair_t **ppairs)
{
	lmpair_t *pairs = NULL;
	int *ord = malloc(sizeof(int) * (size_t)(numed ? numed : 1));
	int numpairs = 0, maxpairs = 0, g0, g1;

	if (!ord)
	{
		*ppairs = NULL;
		return 0;
	}

	for (g0 = 0; g0 < numed; g0 = g1)
	{
		vec3_t lo, hi;
		int i, k, n, axis, first = numpairs;

		for (g1 = g0 + 1; (g1 < numed) && !LMEdgeCmp(&ed[g0], &ed[g1]); g1++)
		{
		}

		VectorCopy(ed[g0].mn, lo);
		VectorCopy(ed[g0].mx, hi);

		for (i = g0 + 1; i < g1; i++)
		{
			for (k = 0; k < 3; k++)
			{
				lo[k] = Q_min(lo[k], ed[i].mn[k]);
				hi[k] = Q_max(hi[k], ed[i].mx[k]);
			}
		}

		axis = ((hi[0] - lo[0] >= hi[1] - lo[1]) && (hi[0] - lo[0] >= hi[2] - lo[2])) ? 0 :
			(hi[1] - lo[1] >= hi[2] - lo[2]) ? 1 : 2;
		n = g1 - g0;

		for (i = 0; i < n; i++)
		{
			ord[i] = g0 + i;
		}

		lmsweep_ed = ed;
		lmsweep_axis = axis;
		qsort(ord, n, sizeof(int), LMSweepCmp);

		for (i = 0; i < n; i++)
		{
			int a = ord[i];

			for (k = i + 1; (k < n) && (ed[ord[k]].mn[axis] <= ed[a].mx[axis] + 0.25f); k++)
			{
				int x = (a < ord[k]) ? a : ord[k], y = (a < ord[k]) ? ord[k] : a;
				float plo, phi;

				if (!LMPairTest(ed, x, y, &plo, &phi))
				{
					continue;
				}

				if (numpairs == maxpairs)
				{
					lmpair_t *np = realloc(pairs, sizeof(*pairs) * (size_t)(maxpairs ? maxpairs * 2 : 1024));

					if (!np)
					{
						continue;
					}

					pairs = np;
					maxpairs = maxpairs ? maxpairs * 2 : 1024;
				}

				pairs[numpairs].i = x;
				pairs[numpairs].j = y;
				pairs[numpairs].lo = plo;
				pairs[numpairs].hi = phi;
				numpairs++;
			}
		}

		if (numpairs - first > 1)
		{
			qsort(pairs + first, numpairs - first, sizeof(*pairs), LMPairCmp);
		}
	}

	free(ord);
	*ppairs = pairs;
	return numpairs;
}

/* the corrections of one face: full on the samples that make up the border, fading into the face over K samples
   (each face only reads its own sums and writes its own samples: the faces run on all cores) */
typedef struct
{
	float *D;
	int *dist;
	int cap;
} lmcorrbuf_t;

typedef struct
{
	gl3model_t *m;
	float **acc;
	const int *faces;
	int numfaces, K;
} lmcorr_t;

#define LMCORR_FACES 32 /* faces per job */

static void
LMCorrectFace(msurface_t *s, const float *acc, int K, lmcorrbuf_t *b)
{
	int ns, k, n, W, H, it, x, y, c;
	float *D;
	int *dist;

	W = s->lmsize[0];
	H = s->lmsize[1];
	n = W * H;

	for (ns = 0; ns < MAX_LIGHTMAPS_PER_SURFACE && s->styles[ns] != 255; ns++)
	{
	}

	/* the work buffers of the largest face so far of this job */
	if (n > b->cap)
	{
		free(b->D);
		free(b->dist);
		b->D = malloc(sizeof(float) * 3 * n);
		b->dist = malloc(sizeof(int) * n);
		b->cap = (b->D && b->dist) ? n : 0;
	}

	if (!b->cap)
	{
		return;
	}

	D = b->D;
	dist = b->dist;

	for (k = 0; k < ns; k++)
	{
		const float *a = acc + (size_t)k * n * 4;
		byte *smp = s->samples + (size_t)k * n * 3;
		qboolean any = false;

		for (x = 0; x < n; x++)
		{
			if (a[x * 4 + 3] > 0.05f)
			{
				for (c = 0; c < 3; c++)
				{
					D[x * 3 + c] = a[x * 4 + c] / a[x * 4 + 3];
				}

				dist[x] = 0;
				any = true;
			}
			else
			{
				D[x * 3] = D[x * 3 + 1] = D[x * 3 + 2] = 0;
				dist[x] = -1;
			}
		}

		if (!any)
		{
			continue;
		}

		for (it = 1; it <= K; it++)
		{
			qboolean grew = false;

			for (y = 0; y < H; y++)
			{
				for (x = 0; x < W; x++)
				{
					int l = y * W + x, nn = 0, d;
					float sum[3] = {0, 0, 0};
					static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};

					if (dist[l] >= 0)
					{
						continue;
					}

					for (d = 0; d < 4; d++)
					{
						int xx = x + dx[d], yy = y + dy[d], o;

						if ((xx < 0) || (yy < 0) || (xx >= W) || (yy >= H))
						{
							continue;
						}

						o = yy * W + xx;

						if ((dist[o] >= 0) && (dist[o] < it))
						{
							for (c = 0; c < 3; c++)
							{
								sum[c] += D[o * 3 + c];
							}

							nn++;
						}
					}

					if (nn)
					{
						for (c = 0; c < 3; c++)
						{
							D[l * 3 + c] = sum[c] / nn * (1.0f - 1.0f / (K + 1 - it + 1));
						}

						dist[l] = it;
						grew = true;
					}
				}
			}

			if (!grew)
			{
				break; /* nothing reached this time: neither will a later pass */
			}
		}

		for (x = 0; x < n; x++)
		{
			if (dist[x] < 0)
			{
				continue; /* no correction: the sample stays as it is */
			}

			for (c = 0; c < 3; c++)
			{
				int v = (int)floorf(smp[x * 3 + c] + D[x * 3 + c] + 0.5f);

				smp[x * 3 + c] = (byte)Q_clamp(v, 0, 255);
			}
		}
	}
}

/* whether two of the faces' sample blocks overlap */
typedef struct
{
	const byte *lo, *hi;
} lmspan_t;

static int
LMSpanCmp(const void *x, const void *y)
{
	const lmspan_t *p = x, *q = y;

	return (p->lo < q->lo) ? -1 : (p->lo > q->lo) ? 1 : 0;
}

static qboolean
LMSamplesShared(const gl3model_t *m, const int *faces, int numfaces)
{
	lmspan_t *sp = malloc(sizeof(*sp) * (size_t)(numfaces ? numfaces : 1));
	qboolean shared = false;
	int f;

	if (!sp)
	{
		return true;
	}

	for (f = 0; f < numfaces; f++)
	{
		const msurface_t *s = &m->surfaces[faces[f]];
		int ns;

		for (ns = 0; ns < MAX_LIGHTMAPS_PER_SURFACE && s->styles[ns] != 255; ns++)
		{
		}

		sp[f].lo = s->samples;
		sp[f].hi = s->samples + (size_t)3 * s->lmsize[0] * s->lmsize[1] * ns;
	}

	qsort(sp, numfaces, sizeof(*sp), LMSpanCmp);

	for (f = 1; f < numfaces; f++)
	{
		if (sp[f].lo < sp[f - 1].hi)
		{
			shared = true;
			break;
		}
	}

	free(sp);
	return shared;
}

static void
LMCorrectJob(int job, void *ctx)
{
	lmcorr_t *q = (lmcorr_t *)ctx;
	lmcorrbuf_t b = {NULL, NULL, 0};
	int f;

	for (f = job * LMCORR_FACES; (f < (job + 1) * LMCORR_FACES) && (f < q->numfaces); f++)
	{
		int i = q->faces[f];

		LMCorrectFace(&q->m->surfaces[i], q->acc[i], q->K, &b);
	}

	free(b.D);
	free(b.dist);
}

void
GL3_LM_StitchSeams(gl3model_t *m)
{
	cvar_t *on = ri.Cvar_Get("r_om_lmstitch", "1", CVAR_ARCHIVE);
	cvar_t *width = ri.Cvar_Get("r_om_lmstitch_width", "3", CVAR_ARCHIVE);
	lmedge_t *ed;
	lmpair_t *pairs = NULL;
	int *corrfaces = NULL;
	float **acc;   /* per face: per style per sample r g b weight */
	int numed = 0, i, j, round, faces = 0, points = 0, K, numpairs = 0, pi;

	if (!on->value || (m->bspversion != BSPVERSION_SOF) || !m->numsurfaces)
	{
		return;
	}

	K = (int)Q_clamp(width->value, 1.0f, 8.0f);
	ed = malloc(sizeof(*ed) * (size_t)m->numsurfedges);
	acc = calloc(m->numsurfaces, sizeof(*acc));

	if (!ed || !acc)
	{
		free(ed);
		free(acc);
		return;
	}

	for (i = 0; i < m->numsurfaces; i++)
	{
		const msurface_t *s = &m->surfaces[i];

		if (!LMStitchFace(s))
		{
			continue;
		}

		for (j = 0; j < s->numedges; j++)
		{
			int e0 = m->surfedges[s->firstedge + j], e1 = m->surfedges[s->firstedge + (j + 1) % s->numedges];
			const float *va = m->vertexes[(e0 > 0) ? m->edges[e0].v[0] : m->edges[-e0].v[1]].position;
			const float *vb = m->vertexes[(e1 > 0) ? m->edges[e1].v[0] : m->edges[-e1].v[1]].position;
			lmedge_t *e = &ed[numed];
			vec3_t d;
			int k;

			VectorSubtract(vb, va, d);

			if (DotProduct(d, d) < 1.0f)
			{
				continue;
			}

			e->plane = s->plane;
			e->back = (s->flags & SURF_PLANEBACK) ? 1 : 0;
			e->face = i;
			VectorCopy(va, e->a);
			VectorCopy(vb, e->b);

			for (k = 0; k < 3; k++)
			{
				e->mn[k] = Q_min(va[k], vb[k]);
				e->mx[k] = Q_max(va[k], vb[k]);
			}

			numed++;
		}
	}

	qsort(ed, numed, sizeof(*ed), LMEdgeCmp);

	for (round = 0; round < 3; round++)
	{
		for (i = 0; i < m->numsurfaces; i++)
		{
			if (acc[i])
			{
				msurface_t *s = &m->surfaces[i];
				int ns, n = s->lmsize[0] * s->lmsize[1];

				for (ns = 0; ns < MAX_LIGHTMAPS_PER_SURFACE && s->styles[ns] != 255; ns++)
				{
				}

				memset(acc[i], 0, sizeof(float) * 4 * n * ns);
			}
		}

		/* the shared stretches: found once (pairs of edges of a plane, the same as before, in the same order) and
		   walked in every round - the rounds only change the samples */
		if (round == 0)
		{
			numpairs = LMFindPairs(ed, numed, &pairs);
		}

		for (pi = 0; pi < numpairs; pi++)
		{
			{
				vec3_t t;

				i = pairs[pi].i;
				j = pairs[pi].j;
				VectorSubtract(ed[i].b, ed[i].a, t);
				VectorNormalize(t);

				{
					msurface_t *A = &m->surfaces[ed[i].face], *B = &m->surfaces[ed[j].face];
					vec3_t p;
					float lo = pairs[pi].lo, hi = pairs[pi].hi, step, x;
					int ka, kb;

					step = Q_min(A->lmstep[0], B->lmstep[0]) * 0.5f;

					for (ka = 0; ka < MAX_LIGHTMAPS_PER_SURFACE && A->styles[ka] != 255; ka++)
					{
						for (kb = 0; kb < MAX_LIGHTMAPS_PER_SURFACE && B->styles[kb] != 255; kb++)
						{
							if (A->styles[ka] == B->styles[kb])
							{
								break;
							}
						}

						if ((kb == MAX_LIGHTMAPS_PER_SURFACE) || (B->styles[kb] == 255))
						{
							continue;
						}

						for (x = lo + Q_min(step * 0.5f, (hi - lo) * 0.5f); x < hi; x += step)
						{
							int ia[4], ib[4], q, c, na = A->lmsize[0] * A->lmsize[1], nb = B->lmsize[0] * B->lmsize[1];
							float wa[4], wb[4], VA[3] = {0, 0, 0}, VB[3] = {0, 0, 0};
							const byte *sa = A->samples + (size_t)ka * na * 3, *sb = B->samples + (size_t)kb * nb * 3;

							VectorMA(ed[i].a, x, t, p);
							LMTaps(A, p, ia, wa);
							LMTaps(B, p, ib, wb);

							for (q = 0; q < 4; q++)
							{
								for (c = 0; c < 3; c++)
								{
									VA[c] += wa[q] * sa[ia[q] * 3 + c];
									VB[c] += wb[q] * sb[ib[q] * 3 + c];
								}
							}

							/* the samples of each side are moved toward the mean, by their share of the reading */
							for (q = 0; q < 2; q++)
							{
								msurface_t *S = q ? B : A;
								int fi = (int)(S - m->surfaces), k = q ? kb : ka, n = q ? nb : na;
								const int *ix = q ? ib : ia;
								const float *w = q ? wb : wa, *V = q ? VB : VA;
								int z;

								if (!acc[fi])
								{
									int ns;

									for (ns = 0; ns < MAX_LIGHTMAPS_PER_SURFACE && S->styles[ns] != 255; ns++)
									{
									}

									acc[fi] = calloc((size_t)4 * n * ns, sizeof(float));

									if (!acc[fi])
									{
										continue;
									}

									faces++;
								}

								for (z = 0; z < 4; z++)
								{
									float *a = acc[fi] + ((size_t)k * n + ix[z]) * 4;

									for (c = 0; c < 3; c++)
									{
										a[c] += w[z] * (0.5f * (VA[c] + VB[c]) - V[c]);
									}

									a[3] += w[z];
								}
							}

							points++;
						}
					}
				}
			}
		}

		/* the corrections, the faces that got sums */
		{
			lmcorr_t q;

			if (!corrfaces)
			{
				corrfaces = malloc(sizeof(int) * (size_t)m->numsurfaces);
			}

			q.m = m;
			q.acc = acc;
			q.faces = corrfaces;
			q.numfaces = 0;
			q.K = K;

			for (i = 0; corrfaces && (i < m->numsurfaces); i++)
			{
				if (acc[i])
				{
					corrfaces[q.numfaces++] = i;
				}
			}

			/* faces that share lightmap samples (a compiler may point two faces at one block) would be corrected at
			   once by two threads: then one by one, in face order, as before */
			if (LMSamplesShared(m, corrfaces, q.numfaces))
			{
				for (i = 0; i < (q.numfaces + LMCORR_FACES - 1) / LMCORR_FACES; i++)
				{
					LMCorrectJob(i, &q);
				}
			}
			else
			{
				GL3_ParallelFor((q.numfaces + LMCORR_FACES - 1) / LMCORR_FACES, LMCorrectJob, &q);
			}
		}
	}

	for (i = 0; i < m->numsurfaces; i++)
	{
		free(acc[i]);
	}

	free(acc);
	free(ed);
	free(pairs);
	free(corrfaces);
	R_Printf(PRINT_DEVELOPER, "r_om_lmstitch: %d faces, %d border points\n", faces, points / 3);
}

