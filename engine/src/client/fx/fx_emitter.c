/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Client effects: emitter bursts and particle creation from a particle
 * description.
 *
 * Implements: SPEC-400 (Emitters, Particle creation (from an emitter))
 */

#include "fx_local.h"

static float
FX_RandBetween(float a, float b)
{
	return (a == b) ? a : FX_flrand(a, b);
}

static void
FX_ToLocal(const vec3_t v, const vec3_t fwd, const vec3_t right, const vec3_t up, vec3_t out)
{
	vec3_t t;

	t[0] = DotProduct(v, fwd);
	t[1] = DotProduct(v, right);
	t[2] = DotProduct(v, up);
	VectorCopy(t, out);
}

static void
FX_LinkToEmitter(fx_part_t *p, fx_run_t *r, int prim)
{
	fx_slot_t *s = &r->slot[prim];
	int idx = (int)(p - fx_parts), *link;

	p->fx = (int)(r - fx_running);
	p->prim = prim;
	p->next = -1;

	/* append: the chain keeps creation order */
	link = &s->head;

	while (*link >= 0)
	{
		link = &fx_parts[*link].next;
	}

	*link = idx;
	s->live++;
}

static void
FX_CreateParticle(fx_run_t *r, int prim, const vec3_t pos, qboolean attached,
		const vec3_t lpos, const vec3_t fwd, const vec3_t right, const vec3_t up, float es,
		int targets)
{
	const fx_eft_particle_t *d = &r->def->eft.prims[prim].u.emitter.p;
	fx_params_t *prm = &r->p;
	fx_part_t *p;
	float L, A, t, len, speed, b, c, e1, e2;
	vec3_t v, a, n, end, ang, lend;
	int i, flags;

	L = FX_Range(&d->life);

	if (targets & FXT_DURATION)
	{
		L *= es;
	}

	if (L <= 0)
	{
		return;
	}

	/* velocity and acceleration (world vectors; attached particles convert
	   them to the effect frame below) */
	if (d->flags & FXF_CURVE)
	{
		c = FX_Range(&d->vel[1]);
		b = FX_Range(&d->vel[2]);
		VectorSubtract(prm->pos2, pos, v);
		VectorScale(v, 1.0f / L, v);
		VectorMA(v, b, up, v);
		VectorMA(v, c, right, v);
		VectorScale(up, -1.9f * b / L, a);
		VectorMA(a, -1.9f * c / L, right, a);
	}
	else
	{
		VectorScale(fwd, FX_Range(&d->vel[0]), v);
		VectorMA(v, FX_Range(&d->vel[1]), right, v);
		VectorMA(v, FX_Range(&d->vel[2]), up, v);

		if (d->flags & FXF_ABS_ACCEL)
		{
			a[0] = FX_Range(&d->accel[0]);
			a[1] = FX_Range(&d->accel[1]);
			a[2] = FX_Range(&d->accel[2]);
		}
		else
		{
			VectorScale(fwd, FX_Range(&d->accel[0]), a);
			VectorMA(a, FX_Range(&d->accel[1]), right, a);
			VectorMA(a, FX_Range(&d->accel[2]), up, a);
		}
	}

	if (targets & FXT_VELOCITY)
	{
		VectorScale(v, es, v);
	}

	if (targets & FXT_ACCEL)
	{
		VectorScale(a, es, a);
	}

	if (d->ptype == FXPT_DEBRIS)
	{
		/* persistent Ghoul debris lives in the debris ring and never fades */
		FX_VecToAngles(fwd, ang);
		ang[0] += FX_Range(&d->gang[0]);
		ang[1] += FX_Range(&d->gang[1]);
		ang[2] += FX_Range(&d->gang[2]);
		n[0] = FX_Range(&d->gavel[0]);
		n[1] = FX_Range(&d->gavel[1]);
		n[2] = FX_Range(&d->gavel[2]);
		{
			float dsc = FX_Range(&d->size[0]) * ((targets & FXT_HEIGHT) ? es : 1);

			/* gore/gibbody and gore/mpgsplatter ship chunks_meat at height 1.
			   Mesh extents are ~8-14 units (fist-to-head); owner reports them as
			   half-person on dismemberment/gib. Physics boxes in the same .eft
			   are only +/-2..3. Scale hard (~0.08) toward the collision size (~fist); original
			   parity for meat props is uncertain (AMEND-014). */
			if (d->graphic[0] && Q_strcasestr(d->graphic, "chunks_meat"))
			{
				dsc *= 0.08f;
				FX_TraceLog("  emitter debris chunks_meat scale %.3f graphic '%s'\n", dsc, d->graphic);
			}

			FX_AddEmitterDebris(r->def->model[prim], d, pos, ang, v, n, a, dsc);
		}
		return;
	}

	p = FX_AllocParticle();

	if (!p)
	{
		return;
	}

	FX_LinkToEmitter(p, r, prim);
	p->desc = d;
	p->pflags = d->flags;
	p->spawn = fx_now;

	if (d->flags & FXF_ONE_FRAME)
	{
		p->oneframe = true;
		FX_SetLife(p, 5000);
	}
	else
	{
		FX_SetLife(p, L);
	}

	VectorCopy(pos, p->pos);
	VectorCopy(v, p->vel);
	VectorCopy(a, p->accel);
	p->decel = (d->flags & FXF_DECELERATE) != 0;

	/* alpha */
	A = FX_Range(&d->rgba[3]);

	if (targets & FXT_ALPHA)
	{
		A *= es;
	}

	p->astyle = d->alpha_style;
	p->cstyle = d->color_style;
	p->a0 = d->alpha_min_ratio * 255.0f;

	switch (d->alpha_style)
	{
		case 0:
			p->alpha = (int)A;
			p->arate = ((float)(int)A * d->alpha_min_ratio - (float)(int)A) / L;
			break;

		case 1:
			p->alpha = (int)(A * d->alpha_min_ratio);
			p->arate = ((float)(int)A - (float)p->alpha) / L;
			break;

		default:
			p->alpha = 1;
			p->arate = A; /* the peak */
			break;
	}

	if (p->alpha < 0)
	{
		p->alpha = 0;
	}

	if (p->alpha > 255)
	{
		p->alpha = 255;
	}

	/* colour */
	switch (d->color_style)
	{
		case 0:
			t = FX_flrand(0, 1);

			for (i = 0; i < 3; i++)
			{
				p->rgb[i] = (byte)(int)(d->rgba[i].min + t * (d->rgba[i].max - d->rgba[i].min));
			}

			break;

		case 1:
			for (i = 0; i < 3; i++)
			{
				p->rgb[i] = (byte)(int)FX_Range(&d->rgba[i]);
			}

			break;

		default:
			for (i = 0; i < 3; i++)
			{
				p->rgb[i] = (byte)(int)d->rgba[i].min;
			}

			break;
	}

	/* render flags */
	flags = 0;

	if (d->flags & FXF_LIT)
	{
		flags |= AQ_LIT;
	}

	if (d->flags & FXF_ADDITIVE)
	{
		flags |= AQ_ADDITIVE;
	}

	if (d->flags & FXF_SUBTRACTIVE)
	{
		flags |= AQ_DARKEN;
	}

	if (d->flags & FXF_CHEAP)
	{
		flags |= AQ_CHEAP;
	}

	if (d->flags & FXF_NO_ZBUFFER)
	{
		flags |= AQ_NOZ;
	}

	if ((d->flags & FXF_RANDOM_ADD) && (FX_flrand(0, 1) < 0.5f))
	{
		flags |= AQ_ADDITIVE;
	}

	/* collision */
	if (d->flags & FXF_PHYSICS)
	{
		p->physics = true;
		VectorCopy(d->bmin, p->bmin);
		VectorCopy(d->bmax, p->bmax);
		p->elasticity = d->elasticity;
		p->hitents = (d->flags & FXF_ENTITIES) != 0;
	}

	/* trails */
	if (d->trail[0] || (d->flags & (FXF_FADE_TRAILS | FXF_SHRINK_TRAILS)))
	{
		p->trailtime = p->spawn;
		p->trailint = (d->trail_freq > 0) ? 1.0f / d->trail_freq : 1.0f / 60.0f;
	}

	switch (d->ptype)
	{
		case FXPT_LINE:
			p->kind = FXK_LINE;
			flags |= AQ_BEAM;

			if (d->point2 == 1)
			{
				VectorCopy(prm->pos2, end);
			}
			else if (d->point2 == 2)
			{
				VectorClear(end); /* "End of Prev" is not implemented: the world origin */
			}
			else
			{
				speed = VectorLength(v);
				if (speed < 0.1f)
				{
					VectorCopy(a, n);
				}
				else
				{
					VectorCopy(v, n);
				}

				VectorNormalize(n);
				len = FX_Range(&d->endscale);
				VectorMA(pos, len, n, end);

				if ((d->point2 == 0) && (speed >= 0.1f))
				{
					p->lookahead = FX_Range(&d->endscale) / speed;
				}
			}

			VectorCopy(end, p->end);

			if (d->point2 == 3)
			{
				VectorClear(p->vel); /* both ends: acceleration only */
			}

			VectorCopy(p->vel, p->endvel);
			VectorCopy(p->accel, p->endaccel);

			p->d1 = FX_Range(&d->size[0]);
			e1 = FX_Range(&d->size[1]);

			if (targets & FXT_HEIGHT)
			{
				p->d1 *= es;
				e1 *= es;
			}

			if (d->flags & FXF_WIDTH_EQ_HEIGHT)
			{
				p->d2 = p->d1;
				e2 = e1;
			}
			else
			{
				p->d2 = FX_Range(&d->size[2]);
				e2 = FX_Range(&d->size[3]);

				if (targets & FXT_WIDTH)
				{
					p->d2 *= es;
					e2 *= es;
				}
			}

			if (!(d->flags & FXF_NO_SCALING))
			{
				p->r1 = (e1 - p->d1) / L;
				p->r2 = (e2 - p->d2) / L;
			}

			break;

		case FXPT_GHOUL:
			p->kind = FXK_GHOUL;
			p->model = r->def->model[prim];
			p->gscale = FX_Range(&d->size[0]);
			e1 = FX_Range(&d->size[1]);

			if (targets & FXT_HEIGHT)
			{
				p->gscale *= es;
				e1 *= es;
			}

			if (!(d->flags & FXF_NO_SCALING))
			{
				p->gscalerate = (e1 - p->gscale) / L;
			}

			FX_VecToAngles(fwd, ang);
			p->gangles[0] = ang[0] + FX_Range(&d->gang[0]);
			p->gangles[1] = ang[1] + FX_Range(&d->gang[1]);
			p->gangles[2] = FX_Range(&d->gang[2]);

			for (i = 0; i < 3; i++)
			{
				p->gavel[i] = FX_Range(&d->gavel[i]);
			}

			break;

		default: /* sprite, oriented sprite */
			p->kind = FXK_BILLBOARD;
			flags |= AQ_BILLBOARD;

			p->d1 = FX_Range(&d->size[0]);
			e1 = FX_Range(&d->size[1]);

			if (targets & FXT_HEIGHT)
			{
				p->d1 *= es;
				e1 *= es;
			}

			if (d->flags & FXF_WIDTH_EQ_HEIGHT)
			{
				p->d2 = p->d1;
				e2 = e1;
			}
			else
			{
				p->d2 = FX_Range(&d->size[2]);
				e2 = FX_Range(&d->size[3]);

				if (targets & FXT_WIDTH)
				{
					p->d2 *= es;
					e2 *= es;
				}
			}

			if (!(d->flags & FXF_NO_SCALING))
			{
				p->r1 = (e1 - p->d1) / L;
				p->r2 = (e2 - p->d2) / L;
			}

			p->angle = FX_Range(&d->start_angle);
			p->avel = FX_Range(&d->angle_vel);

			if (d->ptype == FXPT_ORIENTED)
			{
				flags |= AQ_ORIENTED;

				switch (d->point2)
				{
					case 1:
						VectorCopy(right, p->q.normal);
						break;

					case 2:
						VectorCopy(up, p->q.normal);
						break;

					case 3:
						VectorNegate(fwd, p->q.normal);
						break;

					default:
						VectorCopy(fwd, p->q.normal);
						break;
				}
			}

			break;
	}

	/* a pool particle starts without an image (SPEC-400 Particle pool 3): the description's graphic
	   gives it one; without a sprite it is freed at the next frame */
	p->noimage = (p->kind != FXK_GHOUL) && !d->graphic[0];

	p->q.texname = d->graphic;
	p->q.flags = (unsigned short)flags;
	p->q.color[0] = p->rgb[0];
	p->q.color[1] = p->rgb[1];
	p->q.color[2] = p->rgb[2];
	p->q.color[3] = (byte)p->alpha;

	/* Ghoul particle types with "Attached To Source" do not work in 1.07f: unattached
	   (SPEC-AMEND-005 I5.16) */
	if (attached && (p->kind != FXK_GHOUL))
	{
		p->attached = true;
		VectorCopy(lpos, p->local);
		FX_ToLocal(p->vel, prm->fwd, prm->right, prm->up, p->vel);
		FX_ToLocal(p->accel, prm->fwd, prm->right, prm->up, p->accel);

		if (p->kind == FXK_LINE)
		{
			VectorSubtract(p->end, pos, lend);
			FX_ToLocal(lend, prm->fwd, prm->right, prm->up, lend);
			VectorAdd(lpos, lend, p->endlocal);
			FX_ToLocal(p->endvel, prm->fwd, prm->right, prm->up, p->endvel);
			FX_ToLocal(p->endaccel, prm->fwd, prm->right, prm->up, p->endaccel);
		}
	}
}

/* distance level of detail: D = squared distance to the view origin */
int
FX_LodCount(int N, float D)
{
	if (D > 262144.0f)
	{
		if (D - 262144.0f > 262144.0f)
		{
			return 1;
		}

		return (int)(N * (1.0f - (D - 262144.0f) / 262144.0f) + 1);
	}

	return N;
}

/* ring element angle in radians (style 3) */
float
FX_RingAngle(int k, int N, float phi)
{
	return 2.0f * (float)M_PI * ((float)(k + 1) / (float)N + phi);
}

void
FX_EmitterBurst(fx_run_t *r, int prim, float t)
{
	const fx_eft_emitter_t *e = &r->def->eft.prims[prim].u.emitter;
	fx_params_t *prm = &r->p;
	float base, radius, D, f, g, es, phi = 0, theta, dist;
	int N, k, hit;
	vec3_t rmin, rmax, fwd, right, up, pos, lpos, off, dir, end, spot;
	vec3_t efwd, eright, eup;
	trace_t tr;
	qboolean attached, have_spot = false;

	base = prm->scale ? prm->scale : 1.0f;
	radius = e->radius;

	if (e->flags & FXE_SCALE_TIME)
	{
		base *= t * e->tscale[0] + (1.0f - t) * e->tscale[1];
		radius *= base;
	}

	N = prm->count ? prm->count : e->elements;

	if (e->flags & FXE_DIST_LOD)
	{
		VectorSubtract(prm->origin, cl.refdef.vieworg, off);
		D = DotProduct(off, off);
		N = FX_LodCount(N, D);
	}

	if ((e->style == 2) && (e->dist_per_unit != 0))
	{
		VectorSubtract(prm->pos2, prm->origin, off);
		N = (int)(VectorLength(off) / e->dist_per_unit);
	}

	if (N < 1)
	{
		N = 1;
	}

	{
		const float *src = prm->has_rmin ? prm->rmin : e->rmin;

		VectorCopy(src, rmin);
		src = prm->has_rmax ? prm->rmax : e->rmax;
		VectorCopy(src, rmax);
	}
	VectorCopy(prm->fwd, efwd);
	VectorCopy(prm->right, eright);
	VectorCopy(prm->up, eup);

	if (e->style == 3)
	{
		phi = FX_flrand(0, 6.28f);
	}

	/* styles 6..8: one trace per burst */
	if ((e->style >= 6) && (e->style <= 8))
	{
		VectorCopy(prm->origin, end);

		if (e->style == 6)
		{
			end[2] -= 512;
		}
		else if (e->style == 7)
		{
			end[2] += 512;
		}
		else
		{
			VectorMA(prm->origin, 512, prm->fwd, end);
		}

		FX_Trace(prm->origin, NULL, NULL, end, FXTR_BRUSHES, &tr, &hit);
		VectorCopy(tr.endpos, spot);
		have_spot = true;

		if ((e->style == 8) && (tr.fraction < 1.0f))
		{
			FX_AxesFromDir(tr.plane.normal, efwd, eright, eup);
		}
	}

	attached = (e->flags & FXE_ATTACHED) && ((e->style == 0) || (e->style == 1) || (e->style == 4));

	for (k = 0; k < N; k++)
	{
		f = (float)(k + 1) / (float)N;

		if (e->flags & FXE_SCALE_ELEMENT)
		{
			g = 1.0f - (float)(k + 1) / (float)N;
			es = (g * e->escale[0] + (1.0f - g) * e->escale[1]) * base;
		}
		else
		{
			es = base;
		}

		off[0] = FX_RandBetween(rmin[0], rmax[0]);
		off[1] = FX_RandBetween(rmin[1], rmax[1]);
		off[2] = FX_RandBetween(rmin[2], rmax[2]);
		VectorClear(lpos);

		switch (e->style)
		{
			case 1:
				VectorCopy(off, lpos);
				break;

			case 4:
				VectorSet(lpos, k * e->dist_per_unit + off[0], off[1], off[2]);
				break;

			default:
				break;
		}

		switch (e->style)
		{
			case 0:
				VectorCopy(prm->origin, pos);
				break;

			case 1:
			case 4:
				VectorMA(prm->origin, lpos[0], efwd, pos);
				VectorMA(pos, lpos[1], eright, pos);
				VectorMA(pos, lpos[2], eup, pos);
				break;

			case 2:
				VectorSubtract(prm->pos2, prm->origin, dir);
				VectorMA(prm->origin, f, dir, pos);
				VectorMA(pos, off[0], efwd, pos);
				VectorMA(pos, off[1], eright, pos);
				VectorMA(pos, off[2], eup, pos);
				break;

			case 3:
				dist = radius;
				theta = FX_RingAngle(k, N, phi);
				pos[0] = prm->origin[0] + dist * cosf(theta);
				pos[1] = prm->origin[1] + dist * sinf(theta);
				pos[2] = prm->origin[2];
				break;

			case 5:
				VectorCopy(prm->pos2, pos);
				break;

			case 6:
			case 7:
			case 8:
				if (have_spot)
				{
					VectorCopy(spot, pos);
				}
				else
				{
					VectorCopy(prm->origin, pos);
				}

				break;

			default:
				VectorCopy(prm->origin, pos);
				break;
		}

		VectorCopy(efwd, fwd);
		VectorCopy(eright, right);
		VectorCopy(eup, up);

		if (e->flags & FXE_DIR_FROM_OFFSET)
		{
			VectorSubtract(pos, prm->origin, dir);

			if (VectorLength(dir) > 0)
			{
				FX_AxesFromDir(dir, fwd, right, up);
			}
		}

		FX_CreateParticle(r, prim, pos, attached, lpos, fwd, right, up, es, e->targets);
	}

	if (fx_trace && (fx_trace->value >= 2))
	{
		FX_TraceLog("  burst '%s' prim %d: %d element(s) style %d type %d graphic '%s' live %d "
				"at (%.0f %.0f %.0f)\n", r->def->name, prim, N, e->style, e->p.ptype, e->p.graphic,
				r->slot[prim].live, prm->origin[0], prm->origin[1], prm->origin[2]);
	}
}
