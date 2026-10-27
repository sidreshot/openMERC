/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Client effects: the particle pool (1024 records), particle behaviours
 * (billboards, beams, Ghoul particles, invisible timers), collisions, trails,
 * attached particles and the hand-off of particles and quad chains to the
 * renderer.
 *
 * Order within a rendered frame: free, simulate, submit. SPEC-400 submits
 * before simulating; simulating first shows the same states one frame earlier
 * and keeps the submitted records unchanged until the renderer has drawn them
 * (the renderer reads and writes the live records, SPEC-002). One Frame
 * particles are still drawn exactly once and the renderer's level-of-detail
 * marks are still read in the following frame.
 *
 * Implements: SPEC-400 (Particle pool, Particle simulation, Rendering
 * hand-off), SPEC-401 (puffs, sparks, timers, spinning particles), SPEC-412
 * s.8-s.10 (record contents the renderer reads)
 */

#include "fx_local.h"

fx_part_t fx_parts[FX_MAX_PARTICLES];
static int fx_free_list[FX_MAX_PARTICLES];
static int fx_num_free;
static int fx_serial;
int fx_stat_freed[3], fx_stat_chains, fx_stat_chainquads;

static sof_alphaquad_t *fx_scene_quads[SOF_MAX_ALPHAQUADS];
static int fx_num_scene_quads;
static sof_quadchain_t *fx_scene_chains[SOF_MAX_QUADCHAINS];
static int fx_num_scene_chains;

#define FX_TWO_PI 6.2831853f

/* ---- verification log (om_fxtrace): what one message created ------------- */

int
FX_ParticleSerial(void)
{
	return fx_serial;
}

/* "  => particles +N (billboard b, line l, ghoul g, timer t) sprites a xN, b xM ..., lights +L"
   for every live particle allocated after `since` (FX_ParticleSerial before the message) */
void
FX_TraceCreated(int since, int lights_since)
{
	int kinds[5] = {0, 0, 0, 0, 0}, n = 0, i, j, nnames = 0;
	const char *names[8];
	int counts[8];
	char text[512];

	if (!fx_trace || !fx_trace->value)
	{
		return;
	}

	for (i = 0; i < FX_MAX_PARTICLES; i++)
	{
		const fx_part_t *p = &fx_parts[i];
		const char *name;

		if ((p->kind == FXK_FREE) || (p->serial <= since))
		{
			continue;
		}

		n++;
		kinds[p->kind]++;
		name = ((p->kind == FXK_BILLBOARD) || (p->kind == FXK_LINE)) && p->q.texname ? p->q.texname : NULL;

		if (!name)
		{
			continue;
		}

		if (!strncmp(name, "textures/sprites/", 17))
		{
			name += 17;
		}

		for (j = 0; j < nnames; j++)
		{
			if (!strcmp(names[j], name))
			{
				break;
			}
		}

		if (j == nnames)
		{
			if (nnames == 8)
			{
				continue;
			}

			names[nnames] = name;
			counts[nnames++] = 0;
		}

		counts[j]++;
	}

	/* always printed (also "+0"): it closes the temp entity's block in the log */
	Com_sprintf(text, sizeof(text), "  => particles +%d (billboard %d, line %d, ghoul %d, timer %d)", n,
			kinds[FXK_BILLBOARD], kinds[FXK_LINE], kinds[FXK_GHOUL], kinds[FXK_TIMER]);

	for (j = 0; j < nnames; j++)
	{
		Q_strlcat(text, va("%s %s x%d", j ? "," : " sprites", names[j], counts[j]), sizeof(text));
	}

	if (cl_sof_lights_added != lights_since)
	{
		Q_strlcat(text, va(", lights +%d", cl_sof_lights_added - lights_since), sizeof(text));
	}

	FX_TraceLog("%s\n", text);
}

/* ---- pool ---------------------------------------------------------------- */

void
FX_ClearParticles(void)
{
	int i;

	for (i = 0; i < FX_MAX_PARTICLES; i++)
	{
		FXG_FreeInstance(fx_parts[i].ginst);
	}

	memset(fx_parts, 0, sizeof(fx_parts));
	fx_num_free = 0;

	for (i = FX_MAX_PARTICLES - 1; i >= 0; i--)
	{
		fx_free_list[fx_num_free++] = i;
	}
}

fx_part_t *
FX_AllocParticle(void)
{
	fx_part_t *p;
	int serial;

	if (!fx_num_free && !fx_parts[0].serial && (fx_parts[0].kind == FXK_FREE))
	{
		FX_ClearParticles(); /* first use */
	}

	if (!fx_num_free || FX_Frozen())
	{
		return NULL;
	}

	p = &fx_parts[fx_free_list[--fx_num_free]];
	serial = ++fx_serial;

	memset(p, 0, sizeof(*p));
	p->serial = serial;
	p->kind = FXK_BILLBOARD;
	p->fx = -1;
	p->prim = -1;
	p->next = -1;
	p->spawn = fx_now;
	p->die = fx_now;
	p->alpha = 255;
	p->rgb[0] = p->rgb[1] = p->rgb[2] = 255;
	p->noimage = true;
	p->q.f18 = 1;
	p->q.f1c = 0;
	p->q.fogcache = -1;
	p->q.leaf = -1;
	p->q.lightdir = AQ_DIR_DEFAULT;

	return p;
}

void
FX_FreeParticle(fx_part_t *p)
{
	int idx = (int)(p - fx_parts), *link;
	fx_run_t *r;

	if (p->kind == FXK_FREE)
	{
		return;
	}

	if ((p->fx >= 0) && (p->fx < FX_MAX_RUNNING))
	{
		r = &fx_running[p->fx];

		if (r->inuse && (p->prim >= 0) && (p->prim < FX_MAX_PRIMS))
		{
			link = &r->slot[p->prim].head;

			while (*link >= 0)
			{
				if (*link == idx)
				{
					*link = p->next;
					r->slot[p->prim].live--;
					break;
				}

				link = &fx_parts[*link].next;
			}
		}
	}

	/* a freed Ghoul particle's model instance is destroyed */
	FXG_FreeInstance(p->ginst);
	p->ginst = NULL;
	p->kind = FXK_FREE;
	p->fx = -1;
	fx_free_list[fx_num_free++] = idx;
}

void
FX_SetLife(fx_part_t *p, float seconds)
{
	p->die = fx_now + seconds;
}

/* once per rendered frame, before submission (SPEC-400 Particle pool item 5) */
void
FX_FreeDeadParticles(void)
{
	int i;
	fx_part_t *p;

	for (i = 0; i < FX_MAX_PARTICLES; i++)
	{
		p = &fx_parts[i];

		if (p->kind == FXK_FREE)
		{
			continue;
		}

		if ((p->die <= fx_now) || (p->alpha < 2) ||
			(p->noimage && (p->kind != FXK_TIMER) && (p->kind != FXK_GHOUL)))
		{
			/* om_fxtrace 2 statistics: why emitter particles end */
			if (p->fx >= 0)
			{
				fx_stat_freed[(p->die <= fx_now) ? 0 : ((p->alpha < 2) ? 1 : 2)]++;

				/* a rising particle (alpha styles 2/3 start at 1) freed before it was ever drawn */
				if ((p->die > fx_now) && (p->alpha < 2) && (p->astyle >= 2) && fx_trace &&
					(fx_trace->value >= 2) && (p->fx < FX_MAX_RUNNING) && fx_running[p->fx].def)
				{
					FX_TraceLog("  freed at alpha %d, age %.3f s: '%s' prim %d alpha style %d peak %.0f min %.0f\n",
							p->alpha, fx_now - p->spawn, fx_running[p->fx].def->name, p->prim, p->astyle,
							p->arate, p->a0);
				}
			}

			FX_FreeParticle(p);
		}
	}
}

/* ---- creation helpers for temp entities (SPEC-401 notation) ------------------ */

void
FX_PuffInit(fx_puff_t *pf)
{
	memset(pf, 0, sizeof(*pf));
	pf->sprite = -1;
	pf->r = pf->g = pf->b = pf->a = 255;
	pf->lightdir = AQ_DIR_DEFAULT;
}

static int
FX_Clamp255(int v)
{
	return v < 0 ? 0 : (v > 255 ? 255 : v);
}

fx_part_t *
FX_Puff(const vec3_t org, const fx_puff_t *pf)
{
	fx_part_t *p = FX_AllocParticle();
	const char *name;
	int flags = AQ_BILLBOARD;

	if (!p)
	{
		return NULL;
	}

	p->kind = FXK_BILLBOARD;
	VectorCopy(org, p->pos);
	VectorCopy(pf->vel, p->vel);
	VectorCopy(pf->accel, p->accel);
	p->d1 = pf->h;
	p->d2 = pf->w;
	p->r1 = p->r2 = pf->grow;
	p->angle = pf->rot;
	p->avel = pf->avel;
	p->arate = pf->arate;
	p->rgb[0] = (byte)FX_Clamp255(pf->r);
	p->rgb[1] = (byte)FX_Clamp255(pf->g);
	p->rgb[2] = (byte)FX_Clamp255(pf->b);
	p->alpha = FX_Clamp255(pf->a);
	FX_SetLife(p, pf->life_ms / 1000.0f);

	name = FX_SpriteName(pf->sprite);

	if (name)
	{
		p->noimage = false;
		p->q.texname = name;
	}
	else
	{
		p->die = fx_now; /* a sprite outside the list: no image, dies at once */
	}

	if (pf->blend == 1)
	{
		flags |= AQ_ADDITIVE;
	}
	else if (pf->blend == 2)
	{
		flags |= AQ_DARKEN;
	}

	if (pf->lit)
	{
		flags |= AQ_LIT;
	}

	if (pf->flat)
	{
		flags |= AQ_ORIENTED;
		VectorSet(p->q.normal, 0, 0, 1);
	}
	else if (pf->normal)
	{
		flags |= AQ_ORIENTED;
		VectorCopy(pf->normal, p->q.normal);
	}

	p->q.flags = (unsigned short)flags;
	p->q.lightdir = (byte)pf->lightdir;

	return p;
}

fx_part_t *
FX_Line(const vec3_t start, const vec3_t end, int sprite, float halfwidth, int r, int g, int b,
		int a, float life_ms, int blend, qboolean lit)
{
	fx_part_t *p = FX_AllocParticle();
	const char *name;
	int flags = AQ_BEAM;

	if (!p)
	{
		return NULL;
	}

	p->kind = FXK_LINE;
	VectorCopy(start, p->pos);
	VectorCopy(end, p->end);
	p->d1 = p->d2 = halfwidth;
	p->rgb[0] = (byte)FX_Clamp255(r);
	p->rgb[1] = (byte)FX_Clamp255(g);
	p->rgb[2] = (byte)FX_Clamp255(b);
	p->alpha = FX_Clamp255(a);
	FX_SetLife(p, life_ms / 1000.0f);

	name = FX_SpriteName(sprite);

	if (name)
	{
		p->noimage = false;
		p->q.texname = name;
	}
	else
	{
		p->die = fx_now;
	}

	if (blend == 1)
	{
		flags |= AQ_ADDITIVE;
	}
	else if (blend == 2)
	{
		flags |= AQ_DARKEN;
	}

	if (lit)
	{
		flags |= AQ_LIT;
	}

	p->q.flags = (unsigned short)flags;
	return p;
}

fx_part_t *
FX_Timer(const vec3_t org, fx_timer_cb_t cb, float interval_ms, float jitter, float life_ms)
{
	fx_part_t *p = FX_AllocParticle();

	if (!p)
	{
		return NULL;
	}

	p->kind = FXK_TIMER;
	VectorCopy(org, p->pos);
	p->timercb = cb;
	p->tinterval = interval_ms;
	p->tjitter = jitter;
	p->tlife = life_ms;
	FX_SetLife(p, life_ms / 1000.0f);
	p->tnext = fx_now + interval_ms / 1000.0f * FX_flrand(1.0f - jitter, 1.0f + jitter);
	return p;
}

/* ---- behaviours ---------------------------------------------------------------- */

static void
FX_StartCollisionEffect(fx_part_t *p, const vec3_t normal)
{
	fx_params_t prm;
	vec3_t back;

	if (!p->desc || !p->desc->collide[0])
	{
		return;
	}

	FX_DefaultParams(&prm);
	VectorCopy(p->pos, prm.origin);
	VectorNegate(normal, back);
	FX_AxesFromDir(back, prm.fwd, prm.right, prm.up);
	FX_StartNamed(p->desc->collide, &prm, FXS_COLLIDE);
}

/* Moves pos by dt * vel with a box trace. Returns 0 = moved freely,
   1 = hit (bounced, not moved), 2 = killed. */
static int
FX_CollideMove(fx_part_t *p, vec3_t pos, vec3_t vel, float dt, qboolean removeonhit)
{
	vec3_t end, n;
	trace_t tr;
	int hit, i;
	float vn;

	if (!vel[0] && !vel[1] && !vel[2])
	{
		return 0;
	}

	VectorMA(pos, dt, vel, end);
	FX_Trace(pos, p->bmin, p->bmax, end, p->hitents ? FXTR_SOLID : FXTR_BRUSHES, &tr, &hit);

	if ((tr.fraction > 0.99f) && !tr.startsolid)
	{
		VectorCopy(end, pos);
		return 0;
	}

	VectorCopy(tr.plane.normal, n);
	FX_Bounce(vel, n, p->elasticity);
	(void)vn;
	(void)i;

	if (tr.surface && (tr.surface->flags & (SURF_SKY | SURF_NODRAW)))
	{
		p->die = fx_now;
		p->alpha = 0;
		return 2;
	}

	FX_StartCollisionEffect(p, n);

	if (removeonhit)
	{
		p->die = fx_now;
		return 2;
	}

	return 1;
}

/* v := e (v - 1.8 (v.n) n); integer components within -29..29: at rest */
void
FX_Bounce(vec3_t vel, const vec3_t n, float e)
{
	float vn = DotProduct(vel, n);
	int i;

	for (i = 0; i < 3; i++)
	{
		vel[i] = e * (vel[i] - 1.8f * vn * n[i]);
	}

	if (((int)vel[0] >= -29) && ((int)vel[0] <= 29) && ((int)vel[1] >= -29) &&
		((int)vel[1] <= 29) && ((int)vel[2] >= -29) && ((int)vel[2] <= 29))
	{
		VectorClear(vel);
	}
}

/* alpha of styles 2 and 3 at life fraction t (already + 0.01) */
float
FX_StyleAlpha(int style, float t, float a0, float peak)
{
	float f;

	if (style == 2)
	{
		return (t < 0.5f) ? a0 + (peak - a0) * 2.0f * t : a0 + (peak - a0) * 2.0f * (1.0f - t);
	}

	f = t < 0 ? 0 : (t > 1 ? 1 : t);
	return a0 + (peak - a0) * sinf((float)M_PI * f);
}

static void
FX_Accelerate(fx_part_t *p, vec3_t vel, const vec3_t accel, float dt, qboolean decel)
{
	float old;

	if (p->stopped || (!accel[0] && !accel[1] && !accel[2]))
	{
		return;
	}

	old = VectorLength(vel);
	VectorMA(vel, dt, accel, vel);

	if (decel && (VectorLength(vel) > old))
	{
		/* "Decelerate to a stop": the particle freezes in place */
		VectorClear(vel);
		VectorClear(p->accel);
		p->stopped = true;
	}
}

static void
FX_AlphaColour(fx_part_t *p, float dt)
{
	float t, v, f;
	int i;
	const fx_eft_particle_t *d = p->desc;

	t = (p->die > p->spawn) ? (fx_now - p->spawn) / (p->die - p->spawn) + 0.01f : 1.01f;

	if (!d || (p->astyle == 0) || (p->astyle == 1))
	{
		v = (float)p->alpha + dt * p->arate;
	}
	else
	{
		v = FX_StyleAlpha(p->astyle, t, p->a0, p->arate);
	}

	p->alpha = FX_Clamp255((int)v);

	if (d && ((p->cstyle == 2) || (p->cstyle == 3)))
	{
		f = t < 0 ? 0 : (t > 1 ? 1 : t);

		if (p->cstyle == 3)
		{
			f = sinf(f * (float)M_PI * 0.5f);
		}

		for (i = 0; i < 3; i++)
		{
			p->rgb[i] = (byte)FX_Clamp255((int)(d->rgba[i].min * (1.0f - f) + d->rgba[i].max * f));
		}
	}
}

static void
FX_TrailCopy(fx_part_t *p)
{
	fx_part_t *c;
	const fx_eft_particle_t *d = p->desc;
	float life = d->trail_life;
	int idx;

	if (life <= 0)
	{
		return;
	}

	idx = (int)(p - fx_parts);
	c = FX_AllocParticle();

	if (!c)
	{
		return;
	}

	p = &fx_parts[idx];
	c->kind = p->kind;
	VectorCopy(p->pos, c->pos);
	VectorCopy(p->end, c->end);
	c->d1 = p->d1;
	c->d2 = p->d2;
	c->angle = p->angle;
	c->rgb[0] = p->rgb[0];
	c->rgb[1] = p->rgb[1];
	c->rgb[2] = p->rgb[2];
	c->alpha = p->alpha;
	c->noimage = p->noimage;
	c->q.texname = p->q.texname;
	c->q.flags = p->q.flags & ~(AQ_HIDDEN | AQ_DRAWN);
	VectorCopy(p->q.normal, c->q.normal);
	FX_SetLife(c, life);

	if (p->kind == FXK_LINE)
	{
		if (d->flags & FXF_FADE_TRAILS)
		{
			c->arate = -(float)c->alpha / life;
		}
		else
		{
			c->r1 = c->r2 = -c->d1 / life;
		}
	}
	else
	{
		if (d->flags & FXF_FADE_TRAILS)
		{
			c->arate = -2.0f * (float)c->alpha; /* gone after 0.5 s */
		}
		else
		{
			c->r1 = c->r2 = -p->d2 / life;
		}
	}
}

static void
FX_Trails(fx_part_t *p, qboolean particles)
{
	fx_params_t prm;
	int idx;

	if (p->trailint <= 0)
	{
		return;
	}

	if (fx_now - p->trailtime <= p->trailint)
	{
		return;
	}

	p->trailtime = fx_now;

	if (p->trailcb)
	{
		p->trailcb(p);
		return;
	}

	if (!p->desc)
	{
		return;
	}

	if (p->desc->trail[0])
	{
		idx = (int)(p - fx_parts);
		FX_DefaultParams(&prm);
		VectorCopy(p->pos, prm.origin);

		if (VectorLength(p->vel) > 0)
		{
			FX_AxesFromDir(p->vel, prm.fwd, prm.right, prm.up);
		}

		prm.mode = FXA_PARTICLE;
		prm.part = idx;
		prm.part_serial = p->serial;
		FX_StartNamed(p->desc->trail, &prm, FXS_TRAIL);
		return;
	}

	if (particles && (p->desc->flags & (FXF_FADE_TRAILS | FXF_SHRINK_TRAILS)))
	{
		FX_TrailCopy(p);
	}
}

static void
FX_RunBillboard(fx_part_t *p, float dt)
{
	if (p->vel[0] || p->vel[1] || p->vel[2])
	{
		if (p->attached)
		{
			VectorMA(p->local, dt, p->vel, p->local);
		}
		else if (p->physics)
		{
			if (FX_CollideMove(p, p->pos, p->vel, dt, (p->pflags & FXF_REMOVE_ON_HIT) != 0) == 2)
			{
				return;
			}
		}
		else
		{
			VectorMA(p->pos, dt, p->vel, p->pos);
		}
	}

	FX_Accelerate(p, p->vel, p->accel, dt, p->decel);

	if (p->avel)
	{
		p->angle += dt * p->avel;
		p->angle = fmodf(p->angle, FX_TWO_PI);

		if (p->angle < 0)
		{
			p->angle += FX_TWO_PI;
		}
	}

	p->d1 += dt * p->r1;
	p->d2 += dt * p->r2;

	if ((p->d1 < 0) || (p->d2 < 0))
	{
		if (p->d1 < 0)
		{
			p->d1 = 0;
		}

		if (p->d2 < 0)
		{
			p->d2 = 0;
		}

		p->die = fx_now;
	}

	FX_AlphaColour(p, dt);

	if (p->pflags & FXF_FLICKER)
	{
		p->angle = FX_flrand(0, 6.28f);
	}

	FX_Trails(p, true);
}

static void
FX_RunLine(fx_part_t *p, float dt)
{
	float T;
	int i;

	FX_Accelerate(p, p->vel, p->accel, dt, p->decel);

	if (!p->stopped)
	{
		VectorMA(p->endvel, dt, p->endaccel, p->endvel);
	}

	if (p->attached)
	{
		/* attached lines (SPEC-AMEND-005 I5.16): with "Apply Physics" the start point is
		   still box-traced in world space along dt v and a hit kills the line; the
		   attachment update then recomputes both ends from the effect's axes, so the
		   velocity has no lasting effect */
		if (p->physics)
		{
			vec3_t pos;

			VectorCopy(p->pos, pos);

			if (FX_CollideMove(p, pos, p->vel, dt, false) != 0)
			{
				p->die = fx_now;
				return;
			}
		}
	}
	else
	{
		if (p->physics)
		{
			if (FX_CollideMove(p, p->pos, p->vel, dt, false) != 0)
			{
				p->die = fx_now; /* any hit kills a line */
				return;
			}
		}
		else
		{
			VectorMA(p->pos, dt, p->vel, p->pos);
		}

		VectorMA(p->end, dt, p->endvel, p->end);
	}

	p->d1 += dt * p->r1;
	p->d2 += dt * p->r2;

	if (p->d1 < 0)
	{
		p->d1 = 0;
		p->die = fx_now;
	}

	FX_AlphaColour(p, dt);

	if (p->lookahead > 0)
	{
		T = p->lookahead;

		for (i = 0; i < 3; i++)
		{
			if (p->attached)
			{
				p->endlocal[i] = p->local[i] + p->vel[i] * T + 0.5f * p->accel[i] * T * T;
			}
			else
			{
				p->end[i] = p->pos[i] + p->vel[i] * T + 0.5f * p->accel[i] * T * T;
			}
		}
	}

	FX_Trails(p, true);
}

static void
FX_RunGhoul(fx_part_t *p, float dt)
{
	int moved;

	FX_Accelerate(p, p->vel, p->accel, dt, p->decel);

	if (p->physics)
	{
		moved = FX_CollideMove(p, p->pos, p->vel, dt, false);

		if (moved == 2)
		{
			return;
		}

		VectorScale(p->gavel, 0.9f, p->gavel);

		if (!p->vel[0] && !p->vel[1] && !p->vel[2])
		{
			VectorClear(p->accel);
			VectorClear(p->gavel);
			p->physics = false;
		}
	}
	else if (p->attached)
	{
		VectorMA(p->local, dt, p->vel, p->local);
	}
	else
	{
		VectorMA(p->pos, dt, p->vel, p->pos);
	}

	VectorMA(p->gangles, dt, p->gavel, p->gangles);

	if (p->gscalerate)
	{
		p->gscale += dt * p->gscalerate;
	}

	if (p->arate)
	{
		FX_AlphaColour(p, dt);
	}

	FX_Trails(p, false);
}

static void
FX_RunTimer(fx_part_t *p)
{
	float f;

	if (fx_now < p->tnext)
	{
		return;
	}

	f = (p->die > p->spawn) ? (p->die - fx_now) / (p->die - p->spawn) : 0;

	if (f < 0)
	{
		f = 0;
	}

	/* the next firing uses the millisecond interval as seconds (original quirk) */
	p->tnext = fx_now + p->tinterval * FX_flrand(1.0f - p->tjitter, 1.0f + p->tjitter);

	if (p->timercb)
	{
		p->timercb(p, f);
	}
}

static void
FX_UpdateAttached(fx_part_t *p)
{
	fx_run_t *r;
	int i;

	if ((p->fx < 0) || (p->fx >= FX_MAX_RUNNING))
	{
		return;
	}

	r = &fx_running[p->fx];

	if (!r->inuse)
	{
		return;
	}

	for (i = 0; i < 3; i++)
	{
		p->pos[i] = r->p.origin[i] + p->local[0] * r->p.fwd[i] + p->local[1] * r->p.right[i] +
			p->local[2] * r->p.up[i];
	}

	if (p->kind == FXK_LINE)
	{
		for (i = 0; i < 3; i++)
		{
			p->end[i] = r->p.origin[i] + p->endlocal[0] * r->p.fwd[i] +
				p->endlocal[1] * r->p.right[i] + p->endlocal[2] * r->p.up[i];
		}
	}
}

void
FX_RunParticles(float dt)
{
	int i;
	fx_part_t *p;

	for (i = 0; i < FX_MAX_PARTICLES; i++)
	{
		p = &fx_parts[i];

		switch (p->kind)
		{
			case FXK_BILLBOARD:
				FX_RunBillboard(p, dt);
				break;

			case FXK_LINE:
				FX_RunLine(p, dt);
				break;

			case FXK_GHOUL:
				FX_RunGhoul(p, dt);
				break;

			case FXK_TIMER:
				FX_RunTimer(p);
				break;

			default:
				break;
		}
	}

	for (i = 0; i < FX_MAX_PARTICLES; i++)
	{
		p = &fx_parts[i];

		if ((p->kind != FXK_FREE) && p->attached)
		{
			FX_UpdateAttached(p);
		}
	}
}

/* ---- hand-off ---------------------------------------------------------------- */

/* Frame order of the original (SPEC-AMEND-005 I5.2): submission (which records are drawn,
   chain boxes, weights and counts), then the effects steps, then RenderFrame, which reads
   the referenced live records. OpenMerc copies the state into each record's quad, so after
   the steps the referenced records are filled again: a record that died during the steps is
   still drawn with its final state, and an emitter particle appended during the steps to a
   chain that already had a drawn particle is linked into that chain (box, weight and count
   keep their pre-step values). Free particles created during the steps are first drawn in
   the next frame. */
static void FX_FillQuad(fx_part_t *p);

void
FX_RefreshScene(void)
{
	int i, idx;
	fx_part_t *p, *prev;
	sof_quadchain_t *c;

	for (i = 0; i < fx_num_scene_quads; i++)
	{
		const char *q = (const char *)fx_scene_quads[i];

		if ((q >= (const char *)fx_parts) && (q < (const char *)(fx_parts + FX_MAX_PARTICLES)))
		{
			p = (fx_part_t *)fx_scene_quads[i]; /* the quad is the record's first member */

			if (p->kind != FXK_FREE)
			{
				FX_FillQuad(p);
			}
		}
	}

	for (i = 0; i < fx_num_scene_chains; i++)
	{
		c = fx_scene_chains[i];

		if (!c->first)
		{
			continue;
		}

		prev = NULL;

		for (idx = (int)((fx_part_t *)c->first - fx_parts); idx >= 0; idx = fx_parts[idx].next)
		{
			p = &fx_parts[idx];

			if ((p->kind == FXK_FREE) || (p->kind == FXK_GHOUL) || p->noimage)
			{
				continue;
			}

			FX_FillQuad(p);

			if (prev)
			{
				prev->q.next = &p->q;
			}

			prev = p;
		}
	}
}

void
FX_BeginScene(void)
{
	fx_num_scene_quads = 0;
	fx_num_scene_chains = 0;
}

qboolean
FX_SceneAddQuad(sof_alphaquad_t *q)
{
	if (fx_num_scene_quads >= SOF_MAX_ALPHAQUADS)
	{
		return false;
	}

	fx_scene_quads[fx_num_scene_quads++] = q;
	return true;
}

qboolean
FX_SceneAddChain(sof_quadchain_t *c)
{
	if (fx_num_scene_chains >= SOF_MAX_QUADCHAINS)
	{
		return false;
	}

	fx_scene_chains[fx_num_scene_chains++] = c;
	return true;
}

int
CLFX_SceneAlphaQuads(sof_alphaquad_t ***list)
{
	*list = fx_scene_quads;
	return fx_num_scene_quads;
}

int
CLFX_SceneQuadChains(sof_quadchain_t ***list)
{
	*list = fx_scene_chains;
	return fx_num_scene_chains;
}

static void
FX_FillQuad(fx_part_t *p)
{
	sof_alphaquad_t *q = &p->q;

	VectorCopy(p->pos, q->origin);

	if (p->kind == FXK_LINE)
	{
		VectorCopy(p->end, q->v0c);
		q->f18 = p->d1;
		q->f1c = 0;
	}
	else
	{
		q->v0c[0] = p->d1;
		q->v0c[1] = p->d2;
		q->v0c[2] = 0;
		q->f18 = cosf(p->angle);
		q->f1c = sinf(p->angle);
	}

	q->color[0] = p->rgb[0];
	q->color[1] = p->rgb[1];
	q->color[2] = p->rgb[2];
	q->color[3] = (byte)p->alpha;
	q->next = NULL;
}

static qboolean fx_oneframe_drawn[FX_MAX_PARTICLES];

void
FX_SubmitParticles(void)
{
	int i;
	fx_part_t *p;

	/* One Frame particles drawn in the previous frame die now */
	for (i = 0; i < FX_MAX_PARTICLES; i++)
	{
		if (fx_oneframe_drawn[i])
		{
			fx_oneframe_drawn[i] = false;
			p = &fx_parts[i];

			if ((p->kind != FXK_FREE) && p->oneframe)
			{
				FX_FreeParticle(p);
			}
		}
	}

	for (i = 0; i < FX_MAX_PARTICLES; i++)
	{
		p = &fx_parts[i];

		if ((p->kind == FXK_FREE) || (p->kind == FXK_TIMER) || (p->fx >= 0))
		{
			continue;
		}

		if ((p->die <= fx_now) || p->noimage)
		{
			continue;
		}

		if (p->kind == FXK_GHOUL)
		{
			FXG_AddParticleEntity(p); /* a renderer entity */
			continue;
		}

		FX_FillQuad(p);

		if (FX_SceneAddQuad(&p->q) && p->oneframe)
		{
			fx_oneframe_drawn[i] = true;
		}
	}
}

void
FX_SubmitChains(void)
{
	int i, j, idx, next;
	fx_run_t *r;
	fx_slot_t *s;
	fx_part_t *p, *first, *prev;
	sof_quadchain_t *c;
	float w;
	qboolean firstline;

	for (i = 0; i < FX_MAX_RUNNING; i++)
	{
		r = &fx_running[i];

		if (!r->inuse)
		{
			continue;
		}

		for (j = 0; j < r->def->eft.count; j++)
		{
			s = &r->slot[j];

			if ((r->def->eft.prims[j].type != FXP_EMITTER) || (s->head < 0))
			{
				continue;
			}

			c = &s->chain;
			memset(c, 0, sizeof(*c));
			c->owner = r;
			first = prev = NULL;
			firstline = true;

			for (idx = s->head; idx >= 0; idx = next)
			{
				p = &fx_parts[idx];
				next = p->next;

				if (p->oneframe && fx_oneframe_drawn[idx])
				{
					fx_oneframe_drawn[idx] = false;
					FX_FreeParticle(p);
					continue;
				}

				if (p->die <= fx_now)
				{
					FX_FreeParticle(p);
					continue;
				}

				if ((p->q.flags & AQ_HIDDEN) && !(p->pflags & FXF_PHYSICS))
				{
					FX_FreeParticle(p); /* dropped by the renderer's level of detail */
					continue;
				}

				if (p->kind == FXK_GHOUL)
				{
					if (!first)
					{
						first = p; /* a chain headed by a Ghoul particle is not added */
					}

					FXG_AddParticleEntity(p); /* a renderer entity */
					continue;
				}

				if (p->noimage)
				{
					continue;
				}

				FX_FillQuad(p);

				if (!first)
				{
					first = p;
					VectorCopy(p->pos, c->mins);
					VectorCopy(p->pos, c->maxs);
				}

				if (first->kind == FXK_GHOUL)
				{
					continue;
				}

				AddPointToBounds(p->pos, c->mins, c->maxs);

				if (p->kind == FXK_LINE)
				{
					AddPointToBounds(p->end, c->mins, c->maxs);
					(void)firstline;
					firstline = false;
				}

				if (prev)
				{
					prev->q.next = &p->q;
				}
				else
				{
					c->first = &p->q;
				}

				prev = p;
				c->count++;

				if ((p->die - fx_now >= 0.05f) && !(p->q.flags & AQ_HIDDEN))
				{
					if (p->kind == FXK_LINE)
					{
						w = 5.0f * p->d1;
					}
					else if (p->q.flags & AQ_ORIENTED)
					{
						w = p->d1 * p->d2 * 0.5f;
					}
					else
					{
						w = p->d1 * p->d2;
					}

					c->weight += w;
				}

				if (p->oneframe)
				{
					fx_oneframe_drawn[idx] = true;
				}
			}

			if (c->first && first && (first->kind != FXK_GHOUL))
			{
				fx_stat_chains++;
				fx_stat_chainquads += c->count;
				FX_SceneAddChain(c);
			}
		}
	}
}
