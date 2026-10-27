/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: fog regions of Soldier of Fortune maps. Regions are
 * BSP lump 21 records; faces that belong to a region carry region polygons
 * (lump 19) on which a translucent overlay is drawn whose alpha grows with the
 * squared distance from the eye, shortened by the region's boundary planes,
 * with optional shimmer, height falloff and a far fog wall; point fog for
 * effects uses the same thickness. Package I4c: gl_fogmode 1 (linear fog on
 * interior faces while they are drawn, overlay on boundary faces only).
 *
 * Implements: SPEC-410 (Fog regions, region record, region polygons),
 *             SPEC-413 (gl_fogmode and the fog override cvars)
 */

#include "header/local.h"

#define MAX_FOG_REGIONS 256
#define MAX_FOG_PLANES 5
#define MAX_FOG_QUEUE 1024

typedef struct
{
	int numverts;
	vec3_t *verts;
} fogpoly_t;

typedef struct
{
	int kind;
	float opacity;       /* 4: maximum opacity 0..1 */
	float amplitude;     /* 8: shimmer amplitude */
	float start;         /* 12: fog start (squared units) */
	float range;         /* 16: squared units per alpha step */
	byte color[4];       /* 24 */
	int flags;           /* 28: 1 shimmer, 2 height falloff */
	float speed;         /* 32: shimmer speed */
	float farwall;       /* 36: non-zero = far fog wall */

	vec3_t mins, maxs, size;
	cplane_t *planes[MAX_FOG_PLANES];
	int numplanes;
	qboolean planemarked[MAX_FOG_PLANES];

	float phase[10], phasespeed[10], w[10];
} fogregion_t;

typedef struct
{
	short region;        /* -1 none */
	short numpolys;
	fogpoly_t *polys;
} fogsurf_t;

static fogregion_t regions[MAX_FOG_REGIONS];
static int numregions;
static fogsurf_t *fogsurfs;      /* per world surface */
static short *leafregion;        /* per world leaf */
static int numfogsurfs, numleafregions;
static gl3model_t *fogworld;

typedef struct
{
	msurface_t *surf;
	int region;
	qboolean model;        /* inline model: vertices in model space */
	vec3_t origin, fwd, right, up;
} fogqueued_t;

static fogqueued_t queue[MAX_FOG_QUEUE];
static int numqueued;

static int viewregion = -1;
static byte regionseen[MAX_FOG_REGIONS]; /* GFX-modern volumetric fog: regions with faces walked this frame */
static int lastregion = -1;      /* region most recently processed by an overlay pass */

/* I4c: fog-region polygons drawn this frame (r_speeds 1 "fp", SPEC-413 §7) */
int gl3_c_fogpolys;

/* far wall candidates of the current pass */
static int wallregion;
static float wallbestD;

static float
FRand(float lo, float hi)
{
	return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
}

void
GL3_Fog_FreeRegions(void)
{
	int i, j;

	if (fogsurfs)
	{
		for (i = 0; i < numfogsurfs; i++)
		{
			for (j = 0; j < fogsurfs[i].numpolys; j++)
			{
				free(fogsurfs[i].polys[j].verts);
			}

			free(fogsurfs[i].polys);
		}

		free(fogsurfs);
	}

	free(leafregion);
	fogsurfs = NULL;
	leafregion = NULL;
	numfogsurfs = numleafregions = numregions = 0;
	fogworld = NULL;
	numqueued = 0;
	viewregion = lastregion = -1;
}

static void
EdgeVertex(gl3model_t *mod, int surfedge, vec3_t out)
{
	int e;

	if ((surfedge < 0) || (surfedge >= mod->numsurfedges))
	{
		VectorClear(out);
		return;
	}

	e = mod->surfedges[surfedge];

	if (e >= 0)
	{
		VectorCopy(mod->vertexes[mod->edges[e].v[0]].position, out);
	}
	else
	{
		VectorCopy(mod->vertexes[mod->edges[-e].v[1]].position, out);
	}
}

/*
 * Called by the BSP loader for IBSP v46 worlds after faces, leafs, edges and
 * surfedges are loaded.
 */
void
GL3_Fog_LoadRegions(gl3model_t *mod, const byte *mod_base, const lump_t *lfaces,
	const lump_t *lpolys, const lump_t *lregions, const lump_t *lleafs)
{
	const dface46_t *faces = (const dface46_t *)(mod_base + lfaces->fileofs);
	const int *polys = (const int *)(mod_base + lpolys->fileofs);
	const byte *rec = mod_base + lregions->fileofs;
	const dleaf46_t *leafs = (const dleaf46_t *)(mod_base + lleafs->fileofs);
	int numpolys = lpolys->filelen / 8;
	int i, j, k;

	GL3_Fog_FreeRegions();

	numregions = lregions->filelen / 76;

	if (numregions > MAX_FOG_REGIONS)
	{
		numregions = MAX_FOG_REGIONS;
	}

	for (i = 0; i < numregions; i++, rec += 76)
	{
		fogregion_t *r = &regions[i];

		memset(r, 0, sizeof(*r));
		r->kind = LittleLong(*(const int *)(rec + 0));
		r->opacity = LittleFloat(*(const float *)(rec + 4));
		r->amplitude = LittleFloat(*(const float *)(rec + 8));
		r->start = LittleFloat(*(const float *)(rec + 12));
		r->range = LittleFloat(*(const float *)(rec + 16));
		memcpy(r->color, rec + 24, 4);
		r->flags = LittleLong(*(const int *)(rec + 28));
		r->speed = LittleFloat(*(const float *)(rec + 32));
		r->farwall = LittleFloat(*(const float *)(rec + 36));
		r->mins[0] = r->mins[1] = r->mins[2] = 999999;
		r->maxs[0] = r->maxs[1] = r->maxs[2] = -999999;

		if (r->flags & 1)
		{
			for (j = 0; j < 10; j++)
			{
				r->phase[j] = FRand(0, 20);
				r->phasespeed[j] = FRand(0.05f, 0.3f);
			}
		}
	}

	fogworld = mod;
	numfogsurfs = mod->numsurfaces;
	fogsurfs = calloc(numfogsurfs, sizeof(*fogsurfs));

	for (i = 0; i < numfogsurfs; i++)
	{
		const dface46_t *f = &faces[i];
		int region = LittleShort(f->volume);
		int first = LittleLong(f->firstpoly);
		int n = LittleShort(f->numpolys) & 0xFFFF;
		msurface_t *surf = &mod->surfaces[i];

		fogsurfs[i].region = -1;

		if ((region < 0) || (region >= numregions))
		{
			continue;
		}

		fogsurfs[i].region = region;

		/* boundary planes: faces with texinfo 0x10000, no duplicates, first 5 */
		if (surf->texinfo->flags & SOFSURF_FOGBOUNDARY)
		{
			fogregion_t *r = &regions[region];

			for (k = 0; k < r->numplanes; k++)
			{
				if (r->planes[k] == surf->plane)
				{
					break;
				}
			}

			if ((k == r->numplanes) && (r->numplanes < MAX_FOG_PLANES))
			{
				r->planes[r->numplanes++] = surf->plane;
			}
		}

		if ((first < 0) || (n <= 0) || (first + n > numpolys))
		{
			continue;
		}

		fogsurfs[i].polys = calloc(n, sizeof(fogpoly_t));
		fogsurfs[i].numpolys = n;

		for (j = 0; j < n; j++)
		{
			int fe = LittleLong(polys[(first + j) * 2]);
			int ne = LittleLong(polys[(first + j) * 2 + 1]);
			fogpoly_t *p = &fogsurfs[i].polys[j];

			if ((ne < 3) || (ne > 64))
			{
				continue;
			}

			p->verts = malloc(ne * sizeof(vec3_t));
			p->numverts = ne;

			for (k = 0; k < ne; k++)
			{
				int a;

				EdgeVertex(mod, fe + k, p->verts[k]);

				for (a = 0; a < 3; a++)
				{
					fogregion_t *r = &regions[region];

					if (p->verts[k][a] < r->mins[a])
					{
						r->mins[a] = p->verts[k][a];
					}

					if (p->verts[k][a] > r->maxs[a])
					{
						r->maxs[a] = p->verts[k][a];
					}
				}
			}
		}
	}

	for (i = 0; i < numregions; i++)
	{
		VectorSubtract(regions[i].maxs, regions[i].mins, regions[i].size);
	}

	numleafregions = lleafs->filelen / (int)sizeof(dleaf46_t);
	leafregion = malloc(numleafregions * sizeof(short));

	for (i = 0; i < numleafregions; i++)
	{
		int v = LittleShort(leafs[i].volume);

		leafregion[i] = ((v >= 0) && (v < numregions)) ? v : -1;
	}
}

static int
LeafRegion(const mleaf_t *leaf)
{
	int idx;

	if (!fogworld || !leafregion || !leaf)
	{
		return -1;
	}

	idx = (int)(leaf - fogworld->leafs);
	return ((idx >= 0) && (idx < numleafregions)) ? leafregion[idx] : -1;
}

int
GL3_Fog_RegionOfPoint(const vec3_t p)
{
	if (!fogworld || (fogworld != gl3_worldmodel) || !numregions)
	{
		return -1;
	}

	return LeafRegion(Mod_PointInLeaf(p, fogworld->nodes));
}

int
GL3_Fog_SurfaceRegion(msurface_t *surf)
{
	int idx;

	if (!fogworld || (fogworld != gl3_worldmodel) || !fogsurfs)
	{
		return -1;
	}

	idx = (int)(surf - fogworld->surfaces);
	return ((idx >= 0) && (idx < numfogsurfs)) ? fogsurfs[idx].region : -1;
}

/* the view leaf of SPEC-410 frame set-up step 3 decides "eye inside a region" */
void
GL3_Fog_BeginFrame(void)
{
	int i, j;
	mleaf_t *leaf, *probe;
	vec3_t p;

	numqueued = 0;
	viewregion = -1;
	memset(regionseen, 0, sizeof(regionseen));

	if (!fogworld || (fogworld != gl3_worldmodel) || !numregions)
	{
		return;
	}

	for (i = 0; i < numregions; i++)
	{
		for (j = 0; j < MAX_FOG_PLANES; j++)
		{
			regions[i].planemarked[j] = false;
		}
	}

	leaf = Mod_PointInLeaf(r_newrefdef.vieworg, fogworld->nodes);
	VectorCopy(r_newrefdef.vieworg, p);
	p[2] += leaf->contents ? 16 : -16;
	probe = Mod_PointInLeaf(p, fogworld->nodes);

	if (!(probe->contents & CONTENTS_SOLID) && (probe->cluster != leaf->cluster))
	{
		leaf = probe;
	}

	viewregion = LeafRegion(leaf);
}

/* a boundary face walked this frame marks its plane */
void
GL3_Fog_MarkBoundaryPlane(msurface_t *surf)
{
	int region = GL3_Fog_SurfaceRegion(surf), k;

	if (region < 0)
	{
		return;
	}

	for (k = 0; k < regions[region].numplanes; k++)
	{
		if (regions[region].planes[k] == surf->plane)
		{
			regions[region].planemarked[k] = true;
		}
	}
}

void
GL3_Fog_QueueSurface(msurface_t *surf, entity_t *ent)
{
	(void)ent;

	int region = GL3_Fog_SurfaceRegion(surf);

	/* I4c: with gl_fogmode 1 only boundary faces get the overlay (interior faces are fogged while drawn) */
	if ((int)gl_fogmode->value == 1 && !(surf->texinfo->flags & SOFSURF_FOGBOUNDARY))
	{
		return;
	}

	if ((region >= 0) && (region < MAX_FOG_REGIONS))
	{
		regionseen[region] = 1;
	}

	if ((numqueued < MAX_FOG_QUEUE) && (region >= 0))
	{
		queue[numqueued].surf = surf;
		queue[numqueued].region = region;
		queue[numqueued].model = false;
		numqueued++;
	}
}

/*
 * A face of an inline brush model: the model's region is the region of the
 * world leaf containing its bounding-box centre plus origin; the eye is
 * taken relative to the model origin (SPEC-410 Fog regions, Inline brush models).
 */
void
GL3_Fog_QueueModelSurface(msurface_t *surf, int region, const vec3_t origin, const vec3_t angles)
{
	int idx;
	vec3_t a;

	if (!fogworld || (fogworld != gl3_worldmodel) || (region < 0) || (numqueued >= MAX_FOG_QUEUE))
	{
		return;
	}

	/* I4c: gl_fogmode 1, see GL3_Fog_QueueSurface */
	if ((int)gl_fogmode->value == 1 && !(surf->texinfo->flags & SOFSURF_FOGBOUNDARY))
	{
		return;
	}

	idx = (int)(surf - fogworld->surfaces);

	if ((idx < 0) || (idx >= numfogsurfs) || !fogsurfs[idx].numpolys)
	{
		return;
	}

	if (region < MAX_FOG_REGIONS)
	{
		regionseen[region] = 1;
	}

	queue[numqueued].surf = surf;
	queue[numqueued].region = region;
	queue[numqueued].model = true;
	VectorCopy(origin, queue[numqueued].origin);
	a[0] = -angles[0];
	a[1] = angles[1];
	a[2] = -angles[2];
	AngleVectors(a, queue[numqueued].fwd, queue[numqueued].right, queue[numqueued].up);
	numqueued++;
}

/* region parameters with the cvar overrides (SPEC-410 Parameters) */
static void
Params(const fogregion_t *r, float *start, float *range, float *maxalpha, float color[4])
{
	*start = (gl_fogstart->value >= 0) ? gl_fogstart->value : r->start;
	*range = (gl_fogend->value >= 0) ? gl_fogend->value : r->range;
	*maxalpha = (gl_fogdensity->value >= 0) ? gl_fogdensity->value * 255.0f : 255.0f * r->opacity;

	color[0] = r->color[0];
	color[1] = r->color[1];
	color[2] = r->color[2];
	color[3] = r->color[3];

	if (gl_fogcolor->value >= 0)
	{
		float c[3] = {0, 0, 0};

		sscanf(gl_fogcolor->string, "%f %f %f", &c[0], &c[1], &c[2]);
		color[0] = c[0];
		color[1] = c[1];
		color[2] = c[2];
	}
}

/* fog thickness D of a point (SPEC-410) */
static float
ThicknessEye(int region, const vec3_t p, const float *eye)
{
	fogregion_t *r = &regions[region];
	vec3_t d;
	float D, D0;
	int k;

	VectorSubtract(p, eye, d);
	D = D0 = DotProduct(d, d);

	if (viewregion != region)
	{
		for (k = 0; k < r->numplanes; k++)
		{
			cplane_t *pl = r->planes[k];
			float a, b;

			if (r->planemarked[k])
			{
				continue;
			}

			a = DotProduct(pl->normal, eye) - pl->dist;
			b = pl->dist - DotProduct(pl->normal, p);

			if ((a != 0) && (b != 0) && ((a > 0) != (b > 0)))
			{
				r->planemarked[k] = true;
				continue;
			}

			if (a * a + b * b > 0)
			{
				float t = D0 * b * b / (a * a + b * b);

				if (t < D)
				{
					D = t;
				}
			}
		}
	}

	if (r->flags & 1)
	{
		int idx = ((int)floorf(fabsf(p[0]) + fabsf(p[1]) + fabsf(p[2]))) % 10;

		D += r->w[idx];
	}

	return D;
}

static float
Thickness(int region, const vec3_t p)
{
	return ThicknessEye(region, p, r_newrefdef.vieworg);
}

static float
OverlayAlpha(int region, const vec3_t p, float D)
{
	fogregion_t *r = &regions[region];
	float start, range, maxalpha, color[4], a;

	Params(r, &start, &range, &maxalpha, color);

	a = (range != 0) ? (D - start) / range : 0;

	if (a < 0)
	{
		a = 0;
	}

	if (a > maxalpha)
	{
		a = maxalpha;
	}

	if ((r->flags & 2) && (r->size[2] > 0))
	{
		float h = (r->maxs[2] - p[2]) / r->size[2];

		a *= h * h;
	}

	return floorf(a);
}

int
GL3_Fog_NumRegions(void)
{
	return (fogworld && (fogworld == gl3_worldmodel)) ? numregions : 0;
}

/*
 * GFX-modern r_om_volfog (gl3_post.c): the regions seen this frame (faces
 * walked, or the eye inside), nearest first, at most max. Per region 8 vec4:
 * mins + extinction per unit, maxs + height falloff, colour (0..1) + maximum
 * opacity, then 5 half-spaces (inside normal, dist) from the boundary planes;
 * unused ones are always inside. The extinction is 2 / the distance at which
 * the overlay reaches its maximum (start + range * max alpha, squared units).
 */
int
GL3_Fog_VolumeRegions(float *out, int max)
{
	int i, j, k, n = 0, idx[MAX_FOG_REGIONS];
	float dist[MAX_FOG_REGIONS];

	if (!fogworld || (fogworld != gl3_worldmodel) || ((int)gl_fogmode->value == 2) ||
		((int)gl_fogmode->value == 3))
	{
		return 0;
	}

	for (i = 0; i < numregions; i++)
	{
		fogregion_t *r = &regions[i];
		float d2 = 0;

		if (!regionseen[i] && (i != viewregion))
		{
			continue;
		}

		for (k = 0; k < 3; k++)
		{
			float e = r_newrefdef.vieworg[k];
			float o = (e < r->mins[k]) ? r->mins[k] - e : ((e > r->maxs[k]) ? e - r->maxs[k] : 0);

			d2 += o * o;
		}

		/* insertion by distance */
		for (j = n; (j > 0) && (dist[j - 1] > d2); j--)
		{
			dist[j] = dist[j - 1];
			idx[j] = idx[j - 1];
		}

		dist[j] = d2;
		idx[j] = i;
		n++;
	}

	if (n > max)
	{
		n = max;
	}

	for (i = 0; i < n; i++)
	{
		fogregion_t *r = &regions[idx[i]];
		float *o = out + i * 32;
		float start, range, maxalpha, color[4], full;

		Params(r, &start, &range, &maxalpha, color);
		full = start + range * maxalpha;
		full = (full > 1) ? sqrtf(full) : 1;

		VectorCopy(r->mins, o);
		o[3] = 2.0f / full;
		VectorCopy(r->maxs, (o + 4));
		o[7] = ((r->flags & 2) && (r->size[2] > 0)) ? 1 : 0;
		o[8] = color[0] / 255.0f;
		o[9] = color[1] / 255.0f;
		o[10] = color[2] / 255.0f;
		o[11] = (range > 0) ? maxalpha / 255.0f : 0;

		for (k = 0; k < MAX_FOG_PLANES; k++)
		{
			float *pl = o + 12 + k * 4;

			pl[0] = pl[1] = pl[2] = 0;
			pl[3] = -1;

			if (k < r->numplanes)
			{
				cplane_t *p = r->planes[k];
				float lo = 1e30f, hi = -1e30f;
				int c;

				/* the inside is the side holding most of the region's box */
				for (c = 0; c < 8; c++)
				{
					vec3_t v;
					float d;

					v[0] = (c & 1) ? r->maxs[0] : r->mins[0];
					v[1] = (c & 2) ? r->maxs[1] : r->mins[1];
					v[2] = (c & 4) ? r->maxs[2] : r->mins[2];
					d = DotProduct(p->normal, v) - p->dist;
					lo = (d < lo) ? d : lo;
					hi = (d > hi) ? d : hi;
				}

				if (hi >= -lo)
				{
					VectorCopy(p->normal, pl);
					pl[3] = p->dist - 1;
				}
				else
				{
					VectorNegate(p->normal, pl);
					pl[3] = -p->dist - 1;
				}
			}
		}
	}

	return n;
}

/* point fog for effects (SPEC-410 Point fog): factor f and colour */
float
GL3_Fog_PointFactor(int region, const vec3_t p, float fogcolor_out[4])
{
	float start, range, maxalpha, color[4], D, a, f;
	int src;

	if ((region < 0) || (region >= numregions) || (gl_fogmode->value == 2))
	{
		return 0;
	}

	D = Thickness(region, p);
	src = (lastregion >= 0) ? lastregion : region;
	Params(&regions[src], &start, &range, &maxalpha, color);

	a = (range != 0) ? (D - start) / range : 0;

	if (a < 0)
	{
		a = 0;
	}

	if (a > maxalpha)
	{
		a = maxalpha;
	}

	f = a / 255.0f;

	if (f > 1)
	{
		f = 1;
	}

	fogcolor_out[0] = color[0] / 255.0f;
	fogcolor_out[1] = color[1] / 255.0f;
	fogcolor_out[2] = color[2] / 255.0f;
	fogcolor_out[3] = color[3] / 255.0f;
	return f;
}

/* I4b: OpenGL linear fog of a region for Ghoul models with ghl_obj_fog 0 (SPEC-411 §5 step 10):
   start = offset 12 + 90, end = offset 16 / offset 4 (offset 16 when offset 4 is 0) */
qboolean
GL3_Fog_RegionLinear(int region, float *start, float *end, float color[4])
{
	const fogregion_t *r;

	if ((region < 0) || (region >= numregions))
	{
		return false;
	}

	r = &regions[region];
	*start = r->start + 90;
	*end = (r->opacity != 0) ? r->range / r->opacity : r->range;
	color[0] = r->color[0] / 255.0f;
	color[1] = r->color[1] / 255.0f;
	color[2] = r->color[2] / 255.0f;
	color[3] = r->color[3] / 255.0f;
	return true;
}

/* ------------------------------------------------------------------
 * I4c: gl_fogmode 1 - interior faces of a region are drawn with OpenGL
 * linear fog (start = offset 12 + 90, end = offset 16 / offset 4, or
 * offset 16 when offset 4 is 0, colour = region colour); boundary faces
 * keep the overlay (SPEC-410 Fog regions). The cvar overrides replace the
 * offsets as for the overlay.
 * ------------------------------------------------------------------ */

static qboolean
HardwareMode(void)
{
	return gl_fogmode && ((int)gl_fogmode->value == 1);
}

/* the region whose linear fog a world face gets while drawn, or -1 (also -1 outside gl_fogmode 1) */
int
GL3_Fog_HardwareRegion(msurface_t *surf)
{
	int region;

	if (!HardwareMode() || (surf->texinfo->flags & SOFSURF_FOGBOUNDARY))
	{
		return -1;
	}

	region = GL3_Fog_SurfaceRegion(surf);
	return ((region >= 0) && (region < numregions)) ? region : -1;
}

/* the same for a face of an inline model in the model's region (faces that carry region polygons) */
int
GL3_Fog_HardwareModelRegion(msurface_t *surf, int modelregion)
{
	int idx;

	if (!HardwareMode() || (modelregion < 0) || (modelregion >= numregions) || !fogworld ||
		(fogworld != gl3_worldmodel) || (surf->texinfo->flags & SOFSURF_FOGBOUNDARY))
	{
		return -1;
	}

	idx = (int)(surf - fogworld->surfaces);

	if ((idx < 0) || (idx >= numfogsurfs) || !fogsurfs[idx].numpolys)
	{
		return -1;
	}

	return modelregion;
}

void
GL3_Fog_HardwareParams(int region, float *start, float *end, float color[3])
{
	const fogregion_t *r = &regions[region];
	float s, range, maxalpha, col[4], opacity;

	Params(r, &s, &range, &maxalpha, col);
	opacity = maxalpha / 255.0f;
	*start = s + 90;
	*end = (opacity != 0) ? range / opacity : range;
	color[0] = col[0] / 255.0f;
	color[1] = col[1] / 255.0f;
	color[2] = col[2] / 255.0f;
}

/* fills the fog fields of the 3D uniform block for a region; returns true if they changed */
qboolean
GL3_Fog_SetHardwareFog(int region, gl3Uni3D_t *u)
{
	hmm_vec4 color, plane, params;
	vec3_t fwd, right, up;
	float start, end, c[3];

	if ((region < 0) || (region >= numregions) || !HardwareMode())
	{
		if (u->fogParams.Z != 0)
		{
			u->fogParams.Z = 0;
			return true;
		}

		return false;
	}

	GL3_Fog_HardwareParams(region, &start, &end, c);
	AngleVectors(r_newrefdef.viewangles, fwd, right, up);
	color = HMM_Vec4(c[0], c[1], c[2], 1);
	plane = HMM_Vec4(fwd[0], fwd[1], fwd[2], -DotProduct(fwd, r_newrefdef.vieworg));
	params = HMM_Vec4(start, end, 1, 0);

	if (!memcmp(&color, &u->fogColor, sizeof(color)) && !memcmp(&plane, &u->fogPlane, sizeof(plane)) &&
		!memcmp(&params, &u->fogParams, sizeof(params)))
	{
		return false;
	}

	u->fogColor = color;
	u->fogPlane = plane;
	u->fogParams = params;
	return true;
}

/* the same for the effect batch (translucent world faces) */
void
GL3_Fog_SetHardwareFxState(int region, gl3fxstate_t *st)
{
	float start, end, c[3];

	if ((region < 0) || (region >= numregions) || !HardwareMode())
	{
		return;
	}

	GL3_Fog_HardwareParams(region, &start, &end, c);
	st->fogmode = 1;
	st->fogstart = start;
	st->fogend = end;
	st->fogcolor[0] = c[0];
	st->fogcolor[1] = c[1];
	st->fogcolor[2] = c[2];
	st->fogcolor[3] = 1;
}

static void
DrawFarWall(int region)
{
	gl3fxstate_t st;
	gl3fxvtx_t v[4];
	vec3_t fwd, right, up, centre;
	float start, range, maxalpha, color[4];
	int i, j;
	static const float corner[4][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}};

	Params(&regions[region], &start, &range, &maxalpha, color);
	AngleVectors(r_newrefdef.viewangles, fwd, right, up);
	VectorMA(r_newrefdef.vieworg, 2000, fwd, centre);

	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 3; j++)
		{
			v[i].pos[j] = centre[j] + 2500 * corner[i][0] * right[j] + 2500 * corner[i][1] * up[j];
		}

		v[i].st[0] = v[i].st[1] = 0;
		v[i].rgba[0] = color[0] / 255.0f;
		v[i].rgba[1] = color[1] / 255.0f;
		v[i].rgba[2] = color[2] / 255.0f;
		v[i].rgba[3] = color[3] / 255.0f;
	}

	GL3_FX_DefaultState(&st);
	st.blend = 1;
	st.alphatest = false;
	GL3_FX_SetState(&st);
	GL3_FX_Add(v, 4, GL_TRIANGLE_FAN);
}

/* gl_fogmode 3: a colour derived from the polygon's identity */
static void
DebugColor(int id, float out[4])
{
	unsigned h = (unsigned)id * 2654435761u;

	out[0] = ((h >> 0) & 255) / 255.0f;
	out[1] = ((h >> 8) & 255) / 255.0f;
	out[2] = ((h >> 16) & 255) / 255.0f;
	out[3] = 0.8f;
}

static void DrawQueued(int from, int to, int mode);

/* one overlay pass over the queued faces (SPEC-410 World part of a frame, steps 7 and 9) */
void
GL3_Fog_DrawPass(qboolean first)
{
	int i, j, n;
	int mode = (int)gl_fogmode->value;

	n = numqueued;
	numqueued = 0;

	if (first)
	{
		gl3_c_fogpolys = 0; /* SPEC-AMEND-006 I4c.4: "fp" restarts with the first overlay pass of the frame */
	}

	if (!fogworld || (fogworld != gl3_worldmodel) || (mode == 2) || (n == 0))
	{
		return;
	}

	if (first)
	{
		/* shimmer animation, once per rendered frame */
		for (i = 0; i < numregions; i++)
		{
			fogregion_t *r = &regions[i];
			float speed = (gl_fogtime->value >= 0) ? gl_fogtime->value : r->speed;
			float amp = (gl_fogdensityrange->value >= 0) ? gl_fogdensityrange->value : r->amplitude;

			if (!(r->flags & 1))
			{
				continue;
			}

			for (j = 0; j < 10; j++)
			{
				float c;

				r->phase[j] += r->phasespeed[j] * speed;
				c = cosf(r->phase[j]) * amp;
				r->w[j] = c * fabsf(c);
			}
		}
	}

	wallregion = -1;
	wallbestD = 1e30f;

	DrawQueued(0, n, mode);

	if (first && (mode != 3))
	{
		if ((viewregion >= 0) && (regions[viewregion].farwall != 0))
		{
			DrawFarWall(viewregion);
		}
		else if (wallregion >= 0)
		{
			DrawFarWall(wallregion);
		}
	}

	GL3_FX_Flush();
}

/*
 * I4c: the overlay of one translucent world face, drawn right after the face in
 * the sorted pass (SPEC-410 Alpha surfaces: "The fog overlay for the surface is
 * drawn immediately after it"). The world walk also queues such faces for the
 * first overlay pass (classification step 3), so the original draws their
 * overlay twice: black box, r_speeds 1 "fp" in irq3b 659 and the kos1 water
 * pixels match only with both.
 */
void
GL3_Fog_DrawSurfaceNow(msurface_t *surf)
{
	int mode = (int)gl_fogmode->value;
	int region;

	if (!fogworld || (fogworld != gl3_worldmodel) || (mode == 2) || (numqueued >= MAX_FOG_QUEUE))
	{
		return;
	}

	/* gl_fogmode 1: interior faces get linear fog while drawn, only boundary faces an overlay */
	if ((mode == 1) && !(surf->texinfo->flags & SOFSURF_FOGBOUNDARY))
	{
		return;
	}

	region = GL3_Fog_SurfaceRegion(surf);

	if ((region < 0) || (region >= numregions))
	{
		return;
	}

	/* the face uses the queue slot after the faces collected so far, which stay queued */
	queue[numqueued].surf = surf;
	queue[numqueued].region = region;
	queue[numqueued].model = false;
	DrawQueued(numqueued, numqueued + 1, mode);
	GL3_FX_Flush();
}

/*
 * GFX-modern r_om_ppfog (docs/impl/GFX-modern.md 10): the overlay alpha of a
 * world face is evaluated per fragment instead of per vertex and interpolated.
 * The CPU pass over the vertices still runs (plane marking, far wall choice);
 * the planes left unmarked after it, the region parameters and the eye go to
 * the effect shader, the shimmer offset of each vertex goes in its alpha.
 * Faces of inline models keep the per-vertex overlay.
 */
static cvar_t *r_om_ppfog;

static qboolean
PerPixel(int mode)
{
	if (!r_om_ppfog)
	{
		r_om_ppfog = ri.Cvar_Get("r_om_ppfog", "0", CVAR_ARCHIVE);
	}

	return (r_om_ppfog->value != 0) && (mode != 3);
}

/* overlay alpha scale: the volumetric fog takes over part of the overlay (gl3_volfog.c) */
static float
OverlayDim(void)
{
	return GL3_VolFog_OverlayDim();
}

static void
PerPixelState(gl3fxstate_t *st, int region, const float *eye, float start, float range, float maxalpha, const float color[4])
{
	fogregion_t *r = &regions[region];
	int k, n = 0;

	(void)color;
	st->ppfog = 1;
	VectorCopy(eye, st->pf0);
	st->pf0[3] = 1;
	st->pf1[0] = start;
	st->pf1[1] = range;
	st->pf1[2] = maxalpha;
	st->pf1[3] = ((r->flags & 2) && (r->size[2] > 0)) ? 1 : 0;
	st->pf2[0] = r->maxs[2];
	st->pf2[1] = r->size[2];
	st->pf2[2] = OverlayDim();
	memset(st->pfpl, 0, sizeof(st->pfpl));

	if (viewregion != region)
	{
		for (k = 0; k < r->numplanes; k++)
		{
			if (!r->planemarked[k])
			{
				VectorCopy(r->planes[k]->normal, st->pfpl[n]);
				st->pfpl[n][3] = r->planes[k]->dist;
				n++;
			}
		}
	}

	st->pf2[3] = (float)n;
}

/* draws the overlay of the queued faces from .. to - 1 */
static void
DrawQueued(int from, int to, int mode)
{
	gl3fxstate_t st;
	int i, j, k;
	qboolean pp = PerPixel(mode);
	float dim = OverlayDim();

	GL3_FX_DefaultState(&st);
	st.blend = 1;
	st.alphatest = false;
	st.polyoffset = (gl_offsetfactor->value != 0) || (gl_offsetunits->value != 0);
	GL3_FX_SetState(&st);

	for (i = from; i < to; i++)
	{
		fogqueued_t *q = &queue[i];
		msurface_t *surf = q->surf;
		int idx = (int)(surf - fogworld->surfaces);
		fogsurf_t *fs = &fogsurfs[idx];
		int region = q->region;
		vec3_t eye;
		fogregion_t *r = &regions[region];
		float start, range, maxalpha, color[4];

		Params(r, &start, &range, &maxalpha, color);
		lastregion = region;

		if (q->model)
		{
			VectorSubtract(r_newrefdef.vieworg, q->origin, eye);
		}
		else
		{
			VectorCopy(r_newrefdef.vieworg, eye);
		}

		for (j = 0; j < fs->numpolys; j++)
		{
			fogpoly_t *p = &fs->polys[j];
			gl3fxvtx_t v[64];

			gl3_c_fogpolys++; /* I4c: r_speeds 1 "fp", every region polygon of a processed face (SPEC-AMEND-006 I4c.4) */

			if (!p->verts)
			{
				continue;
			}

			for (k = 0; k < p->numverts; k++)
			{
				if (q->model)
				{
					const float *lp = p->verts[k];
					int a;

					for (a = 0; a < 3; a++)
					{
						v[k].pos[a] = q->origin[a] + lp[0] * q->fwd[a] - lp[1] * q->right[a] + lp[2] * q->up[a];
					}
				}
				else
				{
					VectorCopy(p->verts[k], v[k].pos);
				}
				v[k].st[0] = v[k].st[1] = 0;

				if (mode == 3)
				{
					DebugColor(idx * 64 + j, v[k].rgba);
					continue;
				}

				{
					float D = ThicknessEye(region, p->verts[k], eye);

					v[k].rgba[0] = color[0] / 255.0f;
					v[k].rgba[1] = color[1] / 255.0f;
					v[k].rgba[2] = color[2] / 255.0f;
					v[k].rgba[3] = OverlayAlpha(region, p->verts[k], D) / 255.0f * dim;

					if (pp && !q->model)
					{
						/* the shader adds the shimmer offset of the vertex to its own thickness */
						v[k].rgba[3] = 0;

						if (r->flags & 1)
						{
							const float *pv = p->verts[k];

							v[k].rgba[3] = r->w[((int)floorf(fabsf(pv[0]) + fabsf(pv[1]) + fabsf(pv[2]))) % 10];
						}
					}

					if ((r->farwall != 0) && (D < wallbestD))
					{
						wallbestD = D;
						wallregion = region;
					}
				}
			}

			if (pp && !q->model && (mode != 3))
			{
				gl3fxstate_t pst = st;

				PerPixelState(&pst, region, eye, start, range, maxalpha, color);
				GL3_FX_SetState(&pst);
			}
			else
			{
				GL3_FX_SetState(&st);
			}

			GL3_FX_Add(v, p->numverts, GL_TRIANGLE_FAN);
		}
	}
}

/* ------------------------------------------------------------------
 * GFX-modern measurement aids (test harness, not used by the game):
 * r_om_fog_regions lists the regions; r_om_viewtest "x y z pitch yaw" or
 * "r <region> [back] [pitch]" (blanks or '_') moves the rendered eye (single player only)
 * so that fog effects can be captured from fixed views.
 * ------------------------------------------------------------------ */

void
GL3_Fog_Regions_f(void)
{
	int i;

	R_Printf(PRINT_ALL, "%d fog regions, view region %d\n", GL3_Fog_NumRegions(), viewregion);

	for (i = 0; i < GL3_Fog_NumRegions(); i++)
	{
		const fogregion_t *r = &regions[i];

		R_Printf(PRINT_ALL, "%3d (%.0f %.0f %.0f)-(%.0f %.0f %.0f) op %.2f st %.0f rg %.0f fl %d pl %d wall %.0f col %d %d %d\n",
			i, r->mins[0], r->mins[1], r->mins[2], r->maxs[0], r->maxs[1], r->maxs[2], r->opacity, r->start,
			r->range, r->flags, r->numplanes, r->farwall, r->color[0], r->color[1], r->color[2]);
	}
}

void
GL3_Fog_ViewTest(refdef_t *rd)
{
	static cvar_t *vt, *dm, *mc;
	float v[5] = {0, 0, 0, 0, 0};
	char str[128], *c;

	if (!vt)
	{
		vt = ri.Cvar_Get("r_om_viewtest", "0", 0);
		dm = ri.Cvar_Get("deathmatch", "0", 0);
		mc = ri.Cvar_Get("coop", "0", 0); /* maxclients is 16 in SoF single player */
	}

	if (!vt->string[0] || (vt->string[0] == '0') || (rd->rdflags & RDF_NOWORLDMODEL) || dm->value || mc->value)
	{
		return;
	}

	/* '_' separates like a blank (console scripts without quotes) */
	Q_strlcpy(str, vt->string, sizeof(str));

	for (c = str; *c; c++)
	{
		if (*c == '_')
		{
			*c = ' ';
		}
	}

	if (str[0] == 'r')
	{
		int idx = 0;
		float back = 0, pitch = 0;
		const fogregion_t *r;

		sscanf(str + 1, "%d %f %f", &idx, &back, &pitch);

		if ((idx < 0) || (idx >= GL3_Fog_NumRegions()))
		{
			return;
		}

		r = &regions[idx];
		v[0] = (r->mins[0] + r->maxs[0]) * 0.5f;
		v[1] = (r->mins[1] + r->maxs[1]) * 0.5f;
		v[2] = r->mins[2] + Q_min(48.0f, r->size[2] * 0.5f);
		v[3] = pitch;

		/* look along the longer horizontal axis, stepped back by back units */
		if (r->size[0] >= r->size[1])
		{
			v[4] = 0;
			v[0] -= back;
		}
		else
		{
			v[4] = 90;
			v[1] -= back;
		}
	}
	else
	{
		sscanf(str, "%f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4]);
	}

	rd->vieworg[0] = v[0];
	rd->vieworg[1] = v[1];
	rd->vieworg[2] = v[2];
	rd->viewangles[0] = v[3];
	rd->viewangles[1] = v[4];
	rd->viewangles[2] = 0;
}
