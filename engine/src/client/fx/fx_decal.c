/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Client effects: the decal ring (marks): refusal rules, edge fitting,
 * growth, entity cull, hand-off as alpha quads.
 *
 * Implements: SPEC-400 (Decals (marks), Particle pool item 1, Rendering
 * hand-off item 1)
 */

#include "fx_local.h"

typedef struct
{
	qboolean alive;
	sof_alphaquad_t q;
	int ent;
	float born;
	float start, end, accel, time, r0;
	qboolean grow;
} fx_decal_t;

static fx_decal_t fx_decals[FX_MAX_DECALS];
static int fx_decal_next;
static int fx_decal_ring = 256;
static char fx_decal_names[FX_MAX_DECALS][MAX_QPATH];

void
FX_ClearDecals(void)
{
	int n = cl_max_quads ? (int)cl_max_quads->value : 256;

	memset(fx_decals, 0, sizeof(fx_decals));
	fx_decal_next = 0;
	fx_decal_ring = n < 1 ? 1 : (n > FX_MAX_DECALS ? FX_MAX_DECALS : n);
}

/* largest s in 1.0, 0.8, ... that keeps the decal on its surface in one direction. The last
   tried factor is the single-precision residue 3.13e-8 (1.2 minus five times 0.2 and twice 0.1);
   when that trace misses too, one more trace is made and the factor is -0.0999999717, which
   makes the fitted size negative (SPEC-AMEND-005 I5.13) */
static float
FX_FitDirection(const vec3_t point, const vec3_t normal, const vec3_t dir, float size)
{
	static const float scales[] = {1.0f, 0.8f, 0.6f, 0.4f, 0.2f, 0.1f, 3.13e-8f, -0.0999999717f};
	vec3_t start, end;
	trace_t tr;
	int i, hit;

	for (i = 0; i < (int)(sizeof(scales) / sizeof(scales[0])); i++)
	{
		VectorMA(point, scales[i] * size, dir, start);
		VectorMA(start, -2.0f, normal, end);
		FX_Trace(start, NULL, NULL, end, FXTR_BRUSHES, &tr, &hit);

		if (tr.fraction <= 0.99f)
		{
			return scales[i];
		}
	}

	return scales[i - 1];
}

static float
FX_FitSize(const vec3_t point, const vec3_t normal, float size)
{
	vec3_t p, q, d;
	float s, best = 1.0f;
	int i;

	FX_Perpendicular(normal, p);
	CrossProduct(normal, p, q);

	for (i = 0; i < 4; i++)
	{
		switch (i)
		{
			case 0: VectorCopy(p, d); break;
			case 1: VectorNegate(p, d); break;
			case 2: VectorCopy(q, d); break;
			default: VectorNegate(q, d); break;
		}

		s = FX_FitDirection(point, normal, d, size);

		if (s < best)
		{
			best = s;
		}
	}

	return best * size;
}

void
FX_AddDecal(const fx_impact_t *im, const char *sprite, int r, int g, int b, float alpha,
		float size, float endsize, float accel, float time, float rotation)
{
	fx_decal_t *d;
	vec3_t back;
	float big, fitted;
	int slot;

	if (!sprite || !sprite[0])
	{
		return;
	}

	/* refusals (SPEC-400 Decals item 1) */
	if ((im->kind == 1) || (im->kind == 3))
	{
		return;
	}

	if ((im->kind == 0) && (im->surfflags & (SURF_SKY | SURF_WARP | SURF_FLOWING)))
	{
		return;
	}

	if ((im->kind == 2) && (im->material != 37))
	{
		return;
	}

	if (FX_Frozen())
	{
		return;
	}

	/* fitting (item 4) */
	if ((size > 3) || ((endsize != -1) && (endsize > 3)))
	{
		if (endsize != -1)
		{
			big = size > endsize ? size : endsize;
			fitted = FX_FitSize(im->point, im->normal, big);

			if (size > endsize)
			{
				size = fitted;
				endsize = endsize > fitted ? fitted : endsize;
			}
			else
			{
				endsize = fitted;
				size = size > fitted ? fitted : size;
			}
		}
		else
		{
			size = FX_FitSize(im->point, im->normal, size);
		}
	}

	slot = fx_decal_next;
	fx_decal_next = (fx_decal_next + 1) % fx_decal_ring;
	d = &fx_decals[slot];
	memset(d, 0, sizeof(*d));
	Q_strlcpy(fx_decal_names[slot], sprite, MAX_QPATH);

	d->alive = true;
	d->ent = (im->kind == 2) ? im->ent : -1;
	d->born = fx_now;
	d->start = size;

	if ((endsize != -1) && (time > 0))
	{
		d->grow = true;
		d->end = endsize;
		d->accel = accel;
		d->time = time;
		d->r0 = (endsize - time * time * accel / 2.0f - size) / time;
	}

	VectorCopy(im->point, d->q.origin);
	d->q.v0c[0] = d->q.v0c[1] = size;
	d->q.f18 = cosf(rotation);
	d->q.f1c = sinf(rotation);
	VectorCopy(im->normal, d->q.normal);
	d->q.color[0] = (byte)r;
	d->q.color[1] = (byte)g;
	d->q.color[2] = (byte)b;
	d->q.color[3] = (byte)(int)(alpha < 0 ? 0 : (alpha > 255 ? 255 : alpha));
	d->q.texname = fx_decal_names[slot];
	d->q.flags = AQ_BILLBOARD | AQ_ORIENTED | AQ_LIT | AQ_DECALOFS | AQ_NOSORT;
	VectorNegate(im->normal, back);
	d->q.lightdir = (byte)FX_DirIndex(back);
	d->q.fogcache = -1;
	d->q.leaf = im->leaf;

	FX_TraceLog("  decal %s size %.2f (end %.2f) rgba %d %d %d %d at (%.1f %.1f %.1f)\n", sprite, size,
			endsize, r, g, b, d->q.color[3], im->point[0], im->point[1], im->point[2]);
}

/* size(tau) = start + r0 tau + accel tau^2 / 2, r0 = (end - T^2 accel / 2 - start) / T */
float
FX_DecalGrowSize(float start, float end, float accel, float time, float tau)
{
	float r0;

	if (time <= 0)
	{
		return start;
	}

	if (tau >= time)
	{
		return end;
	}

	r0 = (end - time * time * accel / 2.0f - start) / time;
	return start + r0 * tau + accel * tau * tau / 2.0f;
}

void
FX_RunDecals(float dt)
{
	int i;
	fx_decal_t *d;
	float tau, size;

	(void)dt;

	for (i = 0; i < fx_decal_ring; i++)
	{
		d = &fx_decals[i];

		if (!d->alive)
		{
			continue;
		}

		if ((d->ent > 0) && !FX_EntityInFrame(d->ent))
		{
			d->alive = false; /* its brush entity left the frame */
			continue;
		}

		if (d->grow)
		{
			tau = fx_now - d->born;
			size = FX_DecalGrowSize(d->start, d->end, d->accel, d->time, tau);

			if (tau >= d->time)
			{
				d->grow = false;
			}

			d->q.v0c[0] = d->q.v0c[1] = size;
		}
	}
}

int
FX_SubmitDecals(sof_alphaquad_t **list, int n, int max)
{
	int i;

	(void)list;
	(void)max;

	for (i = 0; i < fx_decal_ring; i++)
	{
		if (fx_decals[i].alive)
		{
			if (!FX_SceneAddQuad(&fx_decals[i].q))
			{
				break;
			}

			n++;
		}
	}

	return n;
}
