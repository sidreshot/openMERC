/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Client effects: the debris ring (Ghoul model pieces): creation, motion with
 * collision classes, settling, blowing paper, the on-screen cap. Drawing goes
 * through the Ghoul shim (fx_ghoul.c), which has no Ghoul runtime yet.
 *
 * Implements: SPEC-400 (Debris, Particle pool item 1, Rendering hand-off
 * item 2, Particle simulation item 3), SPEC-401 (chunk settling, temp entity
 * 48 falling-leaf behaviour), SPEC-AMEND-005 I5.12, I5.14
 */

#include "fx_local.h"

static fx_debris_t fx_debris[FX_MAX_DEBRIS];
static int fx_debris_next;
static int fx_debris_ring = 128;
static void *fx_debris_handles[FX_NUM_DEBRIS_MODELS];
static qboolean fx_debris_tried[FX_NUM_DEBRIS_MODELS];

/* collision classes (box, elasticity), SPEC-400 Debris item 4 */
static const struct
{
	float mins[3], maxs[3], elasticity;
} fx_cclass[9] = {
	{{0, 0, 0}, {0, 0, 0}, 0.7f},
	{{0, 0, 0}, {0, 0, 0}, 0.7f},
	{{-2, -2, -2}, {2, 2, 2}, 0.7f},
	{{-10, -10, -10}, {10, 10, 10}, 0.7f},
	{{0, 0, 0}, {0, 0, 0}, 0.95f},
	{{-4, -2, -3}, {4, 2, 3}, 0.7f},
	{{0, 0, 0}, {0, 0, 0}, 0.7f},
	{{-5, -5, -5}, {5, 5, 5}, 0.7f},
	{{-10, -10, -10}, {10, 10, 10}, 0.7f}
};

void
FX_ClearDebris(void)
{
	int n = cl_max_debris ? (int)cl_max_debris->value : 128, i;

	for (i = 0; i < FX_MAX_DEBRIS; i++)
	{
		FXG_FreeInstance(fx_debris[i].ginst);
	}

	memset(fx_debris, 0, sizeof(fx_debris));
	fx_debris_next = 0;
	fx_debris_ring = n < 1 ? 1 : (n > FX_MAX_DEBRIS ? FX_MAX_DEBRIS : n);
	memset(fx_debris_handles, 0, sizeof(fx_debris_handles));
	memset(fx_debris_tried, 0, sizeof(fx_debris_tried));
}

/* test access (om_fxselftest) */
qboolean
FX_DebrisSlotAlive(int slot)
{
	return (slot >= 0) && (slot < fx_debris_ring) && fx_debris[slot].alive;
}

static void *
FX_DebrisModel(int model)
{
	if ((model < 0) || (model >= FX_NUM_DEBRIS_MODELS))
	{
		return NULL;
	}

	if (!fx_debris_tried[model])
	{
		fx_debris_tried[model] = true;
		fx_debris_handles[model] = FXG_RegisterModel(fx_debris_models[model].dir,
				fx_debris_models[model].file);
	}

	return fx_debris_handles[model];
}

/* level start: rock small, rock tiny and the shell casing always; the
   material-dependent models are registered on first use */
void
FX_PreloadDebris(void)
{
	FX_DebrisModel(7);
	FX_DebrisModel(10);
	FX_DebrisModel(18);
}

fx_debris_t *
FX_AddDebris(int model, const vec3_t org, const vec3_t angles, const vec3_t vel,
		const vec3_t avel, float gravity, int cclass)
{
	fx_debris_t *d;

	if (FX_Frozen())
	{
		return NULL;
	}

	d = &fx_debris[fx_debris_next];
	fx_debris_next = (fx_debris_next + 1) % fx_debris_ring;

	FXG_FreeInstance(d->ginst); /* the slot's previous piece */
	memset(d, 0, sizeof(*d));
	d->alive = true;
	d->model = model;
	d->part = 0;
	VectorCopy(org, d->pos);
	VectorCopy(angles, d->angles);
	VectorCopy(vel, d->vel);
	VectorCopy(avel, d->avel);
	d->gravity = gravity;
	d->cclass = (cclass >= 0 && cclass <= 8) ? cclass : 1;
	d->scale = 1;
	d->spawn = fx_now;
	d->inst = FX_DebrisModel(model);
	return d;
}

/* emitter particles of type 4 ("Ghoul Persistent Debris") */
void
FX_AddEmitterDebris(void *model, const fx_eft_particle_t *desc, const vec3_t org,
		const vec3_t angles, const vec3_t vel, const vec3_t avel, const vec3_t accel, float scale)
{
	fx_debris_t *d = FX_AddDebris(-1, org, angles, vel, avel, accel[2], 1);

	if (!d)
	{
		return;
	}

	d->inst = model;
	d->scale = scale;

	if (desc->flags & FXF_PHYSICS)
	{
		d->cclass = 0; /* the particle box: point-sized class 0 with its elasticity */
	}
}

/* SPEC-401 type 48 sheet after its normal motion of the step (SPEC-AMEND-005 I5.14):
   F, U = the sheet's forward and up vectors, F' = (1.4 F.x, 1.4 F.y, 1.3 F.z), k = U.z */
static void
FX_PaperStep(fx_debris_t *d)
{
	vec3_t f, u, fp;
	float pitch, k;
	int i;

	if (DotProduct(d->vel, d->vel) < 1)
	{
		d->alive = false; /* the model is released: the sheet vanishes (on landing) */
		return;
	}

	pitch = fmodf(d->angles[PITCH], 360.0f);

	if (pitch > 180)
	{
		pitch -= 360;
	}
	else if (pitch <= -180)
	{
		pitch += 360;
	}

	if (d->vel[2] > 5)
	{
		/* rising after the throw */
		if (pitch <= -70)
		{
			d->angles[PITCH] = -70;
			d->avel[PITCH] = 0;
		}

		d->swingup = true;
		return;
	}

	d->gravity = -1;
	d->vel[2] = -18;
	d->vel[0] *= 0.9f;
	d->vel[1] *= 0.9f;
	AngleVectors(d->angles, f, NULL, u);
	fp[0] = 1.4f * f[0];
	fp[1] = 1.4f * f[1];
	fp[2] = 1.3f * f[2];
	k = u[2];

	if (d->swingup)
	{
		if (pitch + FX_irand(-15, 15) >= 70)
		{
			d->swingup = false;
		}

		d->vel[2] += 6 * fp[2];

		if (fp[2] > 0)
		{
			for (i = 0; i < 2; i++)
			{
				d->vel[i] += 6 * fp[i];
			}
		}
	}

	if (pitch + FX_irand(-15, 15) <= -70)
	{
		d->swingup = true;
		d->avel[PITCH] = (200 + FX_irand(-50, 50)) * k;
	}
	else
	{
		d->avel[PITCH] = (FX_irand(-50, 50) - 200) * k;
	}

	d->vel[2] -= 6 * fp[2];

	if (fp[2] < 0)
	{
		for (i = 0; i < 2; i++)
		{
			d->vel[i] -= 6 * fp[i];
		}
	}

	if (d->vel[2] >= 5)
	{
		d->vel[2] = 4.5f;
	}
}

static void
FX_RunOneDebris(fx_debris_t *d, float dt)
{
	vec3_t end, mins, maxs, n;
	trace_t tr;
	int hit, i;
	float vn, speed, e, k;

	if (d->rest)
	{
		return;
	}

	d->vel[2] += dt * d->gravity;

	VectorCopy(fx_cclass[d->cclass].mins, mins);
	VectorCopy(fx_cclass[d->cclass].maxs, maxs);
	e = fx_cclass[d->cclass].elasticity;
	VectorMA(d->pos, dt, d->vel, end);
	FX_Trace(d->pos, mins, maxs, end, FXTR_BRUSHES, &tr, &hit);

	if ((tr.fraction > 0.99f) && !tr.startsolid)
	{
		VectorCopy(end, d->pos);
	}
	else
	{
		if (tr.surface && (tr.surface->flags & (SURF_SKY | SURF_NODRAW)))
		{
			d->alive = false;
			return;
		}

		VectorCopy(tr.plane.normal, n);
		FX_Bounce(d->vel, n, e);
		(void)vn;
		(void)i;
	}

	VectorMA(d->angles, dt, d->avel, d->angles);
	VectorScale(d->avel, 0.9f, d->avel);

	speed = VectorLength(d->vel);

	if (d->settle && (speed < 80))
	{
		k = speed / 80.0f;

		if (k < 0.1f)
		{
			d->angles[0] = d->angles[2] = 0; /* lies flat */
		}
		else
		{
			d->angles[0] *= k;
			d->angles[2] *= k;
		}
	}

	if (!d->vel[0] && !d->vel[1] && !d->vel[2])
	{
		/* at rest: no acceleration, no spin, no collision class */
		d->rest = true;
		VectorClear(d->avel);
	}

	if (d->leaf)
	{
		FX_PaperStep(d);
	}
}

void
FX_RunDebris(float dt)
{
	int i;

	for (i = 0; i < fx_debris_ring; i++)
	{
		if (fx_debris[i].alive)
		{
			FX_RunOneDebris(&fx_debris[i], dt);
		}
	}
}

/* visit order W, W - 1, ..., W + 1 with W = the slot written next (once the ring has wrapped:
   the oldest record first, then newest to oldest; SPEC-AMEND-005 I5.12); past
   fx_maxdebrisonscreen + 1 live pieces the rest is destroyed */
void
FX_SubmitDebris(void)
{
	int i, slot, added = 0, cap;
	fx_debris_t *d;

	cap = (fx_maxdebrisonscreen ? (int)fx_maxdebrisonscreen->value : 16) + 1;

	for (i = 0; i < fx_debris_ring; i++)
	{
		slot = (fx_debris_next - i + fx_debris_ring * 2) % fx_debris_ring;
		d = &fx_debris[slot];

		if (!d->alive)
		{
			continue;
		}

		if (added >= cap)
		{
			d->alive = false;
			continue;
		}

		added++;
		FXG_AddDebrisEntity(d);
	}
}

/* om_fxtrace 2 (once a second): every live debris piece */
void
FX_TraceDebris(void)
{
	int i;
	const fx_debris_t *d;

	for (i = 0; i < fx_debris_ring; i++)
	{
		d = &fx_debris[i];

		if (d->alive)
		{
			FX_TraceLog("  debris %d model %d part %d at (%.1f %.1f %.1f) v (%.0f %.0f %.0f)%s age %.2f\n", i,
					d->model, d->part, d->pos[0], d->pos[1], d->pos[2], d->vel[0], d->vel[1], d->vel[2],
					d->rest ? " rest" : "", fx_now - d->spawn);
		}
	}
}
