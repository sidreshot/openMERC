/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GFX-modern r_om_smoothnormals: smooth vertex normals of the world's faces, made once when a map loads.
 *
 * Brush geometry has one normal per face, so a pillar of 8 or 16 faces, an arch or a rounded trim shows its facets
 * in anything that depends on the normal - the highlight of a light, a reflection, the angle term of a muzzle
 * flash. Each corner of a face takes the normals of the faces that touch that point (on their polygon or its edge,
 * T-junctions included) and lie within 60 degrees of its own, so square corners and the floor-to-wall edge stay
 * sharp. A neighbour weighs at most its area over the face's own: a big wall beside a small bevel keeps its own
 * normal (no shading gradient over the whole wall), the bevel and the facets of a column of equal faces round off.
 * Each inline model is smoothed on its own (its faces are in model space). Sky, liquid and nodraw faces are left
 * alone. r_om_smoothnormals 0 (read at map load): the face normals as before.
 */

#include "header/local.h"

#define SM_CELL 128.0f
#define SM_HASH 8192
#define SM_PLANE_EPS 0.25f
#define SM_EDGE_EPS 0.25f
#define SM_COS 0.5f /* 60 degrees */

typedef struct
{
	msurface_t *s;
	vec3_t n;
	float d, area;
	float wsign; /* +1: the polygon wound counter-clockwise about n, -1 the other way */
	vec3_t mins, maxs;
} smface_t;

typedef struct
{
	smface_t *f;
	int nf;
	int *cellstart, *cellitems; /* CSR lists of faces per hash cell */
	vec3_t *out;                /* new normals, in face/vertex order */
	int *vofs;                  /* first vertex of face i in out */
} smctx_t;

static unsigned
CellHash(int x, int y, int z)
{
	return ((unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ (unsigned)z * 83492791u) & (SM_HASH - 1);
}

static int
CellOf(float v)
{
	return (int)floorf(v / SM_CELL);
}

static qboolean
Usable(const msurface_t *s)
{
	return s->polys && !s->polys->next && s->texinfo &&
		!(s->flags & (SURF_DRAWSKY | SURF_DRAWTURB)) &&
		!(s->texinfo->flags & (SURF_SKY | SURF_WARP | SURF_NODRAW));
}

/* does face g touch point p (on its plane, inside its polygon or on an edge)? */
static qboolean
Touches(const smface_t *g, const float *p)
{
	const glpoly_t *poly = g->s->polys;
	int i, n = poly->numverts;

	if (fabsf(DotProduct(g->n, p) - g->d) > SM_PLANE_EPS)
	{
		return false;
	}

	for (i = 0; i < 3; i++)
	{
		if ((p[i] < g->mins[i] - SM_EDGE_EPS) || (p[i] > g->maxs[i] + SM_EDGE_EPS))
		{
			return false;
		}
	}

	for (i = 0; i < n; i++)
	{
		const float *a = poly->vertices[i].pos, *b = poly->vertices[(i + 1) % n].pos;
		vec3_t e, ap, c;
		float len;

		VectorSubtract(b, a, e);
		VectorSubtract(p, a, ap);
		CrossProduct(e, ap, c);
		len = VectorLength(e);

		/* inside: the edge's cross product points along the normal (times the winding's sign) */
		if ((len > 0) && (g->wsign * DotProduct(c, g->n) < -SM_EDGE_EPS * len))
		{
			return false;
		}
	}

	return true;
}

static void
SmoothFace(int fi, void *vctx)
{
	smctx_t *ctx = vctx;
	const smface_t *f = &ctx->f[fi];
	const glpoly_t *poly = f->s->polys;
	int v;

	for (v = 0; v < poly->numverts; v++)
	{
		const float *p = poly->vertices[v].pos;
		unsigned h = CellHash(CellOf(p[0]), CellOf(p[1]), CellOf(p[2]));
		vec3_t sum;
		int k;

		VectorCopy(f->n, sum);

		for (k = ctx->cellstart[h]; k < ctx->cellstart[h + 1]; k++)
		{
			const smface_t *g = &ctx->f[ctx->cellitems[k]];
			float c = DotProduct(f->n, g->n);

			if ((g == f) || (c < SM_COS) || (c > 0.9999f) || !Touches(g, p))
			{
				continue;
			}

			VectorMA(sum, Q_min(1.0f, g->area / Q_max(f->area, 1.0f)), g->n, sum);
		}

		VectorNormalize(sum);
		VectorCopy(sum, ctx->out[ctx->vofs[fi] + v]);
	}
}

/* the polygons' winding against the normal: Touches needs the inside on the side the normal gives */
static float
WindingSign(const glpoly_t *poly, const vec3_t n)
{
	vec3_t a, b, c, sum = {0, 0, 0};
	int i;

	for (i = 1; i + 1 < poly->numverts; i++)
	{
		VectorSubtract(poly->vertices[i].pos, poly->vertices[0].pos, a);
		VectorSubtract(poly->vertices[i + 1].pos, poly->vertices[0].pos, b);
		CrossProduct(a, b, c);
		VectorAdd(sum, c, sum);
	}

	return (DotProduct(sum, n) >= 0) ? 1.0f : -1.0f;
}

static int
SmoothGroup(gl3model_t *mod, int first, int count)
{
	smctx_t ctx;
	int i, nv = 0, total = 0, changed = 0;
	int *cellcount;

	memset(&ctx, 0, sizeof(ctx));
	ctx.f = calloc(count, sizeof(smface_t));
	ctx.vofs = calloc(count + 1, sizeof(int));
	cellcount = calloc(SM_HASH + 1, sizeof(int));
	ctx.cellstart = calloc(SM_HASH + 1, sizeof(int));

	if (!ctx.f || !ctx.vofs || !cellcount || !ctx.cellstart)
	{
		goto done;
	}

	for (i = 0; i < count; i++)
	{
		msurface_t *s = &mod->surfaces[first + i];
		smface_t *f = &ctx.f[ctx.nf];
		const glpoly_t *poly;
		int j, k;

		if (!Usable(s))
		{
			continue;
		}

		poly = s->polys;
		f->s = s;
		VectorCopy(poly->vertices[0].normal, f->n); /* the face normal as built (plane side applied) */
		f->d = DotProduct(f->n, poly->vertices[0].pos);
		VectorCopy(poly->vertices[0].pos, f->mins);
		VectorCopy(poly->vertices[0].pos, f->maxs);
		f->area = 0;

		for (j = 1; j < poly->numverts; j++)
		{
			for (k = 0; k < 3; k++)
			{
				f->mins[k] = Q_min(f->mins[k], poly->vertices[j].pos[k]);
				f->maxs[k] = Q_max(f->maxs[k], poly->vertices[j].pos[k]);
			}

			if (j + 1 < poly->numverts)
			{
				vec3_t a, b, c;

				VectorSubtract(poly->vertices[j].pos, poly->vertices[0].pos, a);
				VectorSubtract(poly->vertices[j + 1].pos, poly->vertices[0].pos, b);
				CrossProduct(a, b, c);
				f->area += 0.5f * VectorLength(c);
			}
		}

		f->wsign = WindingSign(poly, f->n);

		ctx.vofs[ctx.nf] = nv;
		nv += poly->numverts;
		ctx.nf++;
	}

	if (!ctx.nf)
	{
		goto done;
	}

	/* each face into every cell its box (grown by the tolerance) overlaps: count, then fill */
	for (i = 0; i < 2; i++)
	{
		int fi;

		for (fi = 0; fi < ctx.nf; fi++)
		{
			const smface_t *f = &ctx.f[fi];
			int x, y, z;

			for (x = CellOf(f->mins[0] - SM_EDGE_EPS); x <= CellOf(f->maxs[0] + SM_EDGE_EPS); x++)
			for (y = CellOf(f->mins[1] - SM_EDGE_EPS); y <= CellOf(f->maxs[1] + SM_EDGE_EPS); y++)
			for (z = CellOf(f->mins[2] - SM_EDGE_EPS); z <= CellOf(f->maxs[2] + SM_EDGE_EPS); z++)
			{
				unsigned h = CellHash(x, y, z);

				if (i == 0)
				{
					cellcount[h]++;
					total++;
				}
				else
				{
					ctx.cellitems[ctx.cellstart[h] + cellcount[h]++] = fi;
				}
			}
		}

		if (i == 0)
		{
			int h;

			ctx.cellitems = malloc(sizeof(int) * Q_max(total, 1));
			ctx.out = malloc(sizeof(vec3_t) * Q_max(nv, 1));

			if (!ctx.cellitems || !ctx.out)
			{
				goto done;
			}

			for (h = 0; h < SM_HASH; h++)
			{
				ctx.cellstart[h + 1] = ctx.cellstart[h] + cellcount[h];
				cellcount[h] = 0;
			}
		}
	}

	GL3_ParallelFor(ctx.nf, SmoothFace, &ctx);

	for (i = 0; i < ctx.nf; i++)
	{
		glpoly_t *poly = ctx.f[i].s->polys;
		int v;

		for (v = 0; v < poly->numverts; v++)
		{
			const float *n = ctx.out[ctx.vofs[i] + v];

			if (DotProduct(n, poly->vertices[v].normal) < 0.9999f)
			{
				changed++;
			}

			VectorCopy(n, poly->vertices[v].normal);
		}
	}

done:
	free(ctx.f);
	free(ctx.vofs);
	free(cellcount);
	free(ctx.cellstart);
	free(ctx.cellitems);
	free(ctx.out);
	return changed;
}

void
GL3_World_SmoothNormals(gl3model_t *mod)
{
	cvar_t *on = ri.Cvar_Get("r_om_smoothnormals", "1", CVAR_ARCHIVE);
	int i, changed = 0;

	if (!on->value || !mod || !mod->surfaces || !mod->submodels)
	{
		return;
	}

	/* submodel 0 is the world; the others are the inline models, each on its own */
	for (i = 0; i < mod->numsubmodels; i++)
	{
		const gl3model_t *sm = &mod->submodels[i];

		if ((sm->firstmodelsurface < 0) || (sm->nummodelsurfaces <= 0) ||
			(sm->firstmodelsurface + sm->nummodelsurfaces > mod->numsurfaces))
		{
			continue;
		}

		changed += SmoothGroup(mod, sm->firstmodelsurface, sm->nummodelsurfaces);
	}

	R_Printf(PRINT_DEVELOPER, "r_om_smoothnormals: %d corners smoothed\n", changed);
}
