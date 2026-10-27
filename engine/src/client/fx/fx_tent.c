/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Client effects: the 65 temp-entity types of svc_temp_entity, their timer
 * callbacks, burning debris, electric arcs and recursive lightning bolts.
 * Every handler reads exactly the SPEC-201 s.1 payload.
 *
 * Implements: SPEC-401 (Temp entities, Timers, Burning debris, Ghoul
 * explosion models, Electric arc and recursive bolt), SPEC-201 s.1 (payloads)
 */

#include "fx_local.h"

#define C628 ((float)(FX_crand() % 628) / 100.0f)

extern int fx_lim_paper;

static const char *fx_limb_bolts[] = {
	"gbolt_groin", "gbolt_gut_b", "gbolt_Cap_hips_chest", "gbolt_Cap_hips_lthigh",
	"gbolt_Cap_hips_rthigh", "gbolt_Cap_coathips_coatchest", "gbolt_Cap_lcalf_lthigh",
	"gbolt_Cap_lcalf_lfoot", "gbolt_Cap_lfoot_lcalf", "gbolt_Cap_lthigh_lcalf",
	"gbolt_Cap_lthigh_hips", "gbolt_Cap_rcalf_rthigh", "gbolt_Cap_rcalf_rfoot",
	"gbolt_Cap_rfoot_rcalf", "gbolt_Cap_rthigh_rcalf", "gbolt_Cap_rthigh_hips",
	"gbolt_Cap_coatchest_coathips", "gbolt_gut_f", "gbolt_shoulder_lb", "gbolt_shoulder_rb",
	"gbolt_chest_f", "gbolt_chest_b", "gbolt_shoulder_lf", "gbolt_shoulder_rf",
	"gbolt_Cap_chest_head", "gbolt_Cap_chest_hips", "gbolt_Cap_chest_lbicep",
	"gbolt_Cap_chest_rbicep", "gbolt_cap_head_chest", "gbolt_head_b", "gbolt_head_f",
	"gbolt_Cap_lbicep_lforearm", "gbolt_Cap_lhand", "gbolt_Cap_lforearm_lbicep",
	"gbolt_Cap_lforearm_lhand", "gbolt_Cap_lbicep_chest", "gbolt_Cap_rhand",
	"gbolt_Cap_rforearm_rhand", "gbolt_Cap_rforearm_rbicep", "gbolt_Cap_rbicep_chest",
	"gbolt_Cap_rbicep_rforearm"
};

/* ---- small helpers -------------------------------------------------------------- */

static void
FX_RandVec(vec3_t v, float r)
{
	v[0] = FX_flrand(-r, r);
	v[1] = FX_flrand(-r, r);
	v[2] = FX_flrand(-r, r);
}

static fx_part_t *
FX_Flash(const vec3_t org, float size, int r, int g, int b, int a)
{
	fx_puff_t pf;

	FX_PuffInit(&pf);
	pf.sprite = 16; /* flare */
	pf.blend = 1;
	pf.life_ms = 500;
	pf.w = pf.h = size;
	pf.rot = FX_RandRot();
	pf.r = r;
	pf.g = g;
	pf.b = b;
	pf.a = a;
	return FX_Puff(org, &pf);
}

static void
FX_FadeOverLife(fx_part_t *p, float life_ms)
{
	if (p && (life_ms > 0))
	{
		p->arate = -(float)p->alpha / (life_ms / 1000.0f);
	}
}

/* type-0 style spark: per-axis speed irand(smin, smax) */
fx_part_t *
FX_SparkParticle(const vec3_t org, const vec3_t n, int smin, int smax, float size, int alpha,
		float gravity, int sprite, int blend, int cmin, int cmax)
{
	fx_puff_t pf;
	vec3_t o;
	int k, c;

	FX_PuffInit(&pf);

	for (k = 0; k < 3; k++)
	{
		o[k] = org[k] + (float)(FX_crand() % 4 - 1);
		pf.vel[k] = (n[k] + FX_flrand(-0.1f, 0.1f)) * (float)FX_irand(smin, smax) +
			(float)FX_irand(-32, 32);
	}

	VectorSet(pf.accel, 0, 0, gravity);
	pf.sprite = sprite;
	pf.blend = blend;
	pf.life_ms = 1200;
	pf.w = pf.h = size;
	c = FX_irand(cmin, cmax);
	pf.r = pf.g = pf.b = c;
	pf.a = alpha;
	pf.grow = -size * 0.8333f;
	return FX_Puff(o, &pf);
}

static void
FX_SparkBurst(const vec3_t org, const vec3_t n, qboolean blue)
{
	vec3_t o;
	fx_part_t *f;
	int i, c;

	FX_PlaySound(FX_crand() % 2, org, 0.6f, 3.0f, 0, 0, 0);
	VectorAdd(org, n, o);

	if (blue)
	{
		f = FX_Flash(o, 20, FX_irand(120, 170), FX_irand(150, 200), FX_irand(220, 250), 250);
	}
	else
	{
		c = (int)FX_flrand(200, 250);
		f = FX_Flash(o, 20, c, c, c, 250);
	}

	if (f)
	{
		f->avel = FX_flrand(-18.84f, 18.84f);
		f->r1 = f->r2 = -40;
	}

	if (blue)
	{
		CL_SoF_AddLight(0, org, 128, 128, 160, 128, 500, 128);
	}
	else
	{
		CL_SoF_AddLight(0, org, 160, 160, 128, 128, 500, 128);
	}

	for (i = 0; i < 14; i++)
	{
		FX_SparkParticle(org, n, 32, 160, FX_flrand(0.5f, 1.5f), 250, -300, blue ? 15 : 10, 1,
				200, 250);
	}
}

/* orange colour of the explosion sparks (types 19, 33, 38, 44) */
static void
FX_Orange(fx_puff_t *pf)
{
	pf->r = FX_irand(230, 250);
	pf->g = FX_irand(pf->r - 30, pf->r);
	pf->b = FX_irand(pf->g - 30, pf->g);
	pf->a = 250;
}

/* ---- electric arc and recursive bolt ---------------------------------------------- */

/* SPEC-401 Electric arc as corrected by SPEC-AMEND-005 I5.7: sprite `lit` (client sprite 7),
   additive, not world-lit; Q_1 = S_1 + jitter, Q_k = 0.3 S_(k-1) + 0.7 R_k with
   R_k = Q_(k-1) + step + jitter; segment k starts 6 % of (R_k - Q_(k-1)) behind Q_(k-1);
   n - 1 segments, so the arc stops short of `end`; branches (type 12 only) with
   probability 1/5 from the segment's start */
void
FX_ElectricArc(const vec3_t start, const vec3_t end, float seglen, qboolean branches)
{
	vec3_t d, dir, step, p, q, S, Q, Qprev, R, from, tip;
	fx_part_t *l;
	int n, k, J;
	float len;

	VectorSubtract(end, start, d);
	len = VectorLength(d);

	if (seglen <= 0)
	{
		return;
	}

	n = (int)(len / seglen);

	if (n < 2)
	{
		return;
	}

	VectorScale(d, 1.0f / n, step);
	VectorScale(d, 1.0f / len, dir);
	PerpendicularVector(p, dir);
	CrossProduct(dir, p, q);
	J = (int)(1.6f * seglen);

	if (J < 1)
	{
		J = 1;
	}

	VectorClear(Q);

	for (k = 1; k < n; k++)
	{
		float jp = (float)(FX_crand() % J - J / 2);
		float jq = (float)(FX_crand() % J - J / 2);

		VectorCopy(Q, Qprev);

		if (k == 1)
		{
			VectorAdd(start, step, S);
			VectorMA(S, jp, p, Q);
			VectorMA(Q, jq, q, Q);
			VectorCopy(start, from);
		}
		else
		{
			VectorMA(start, (float)(k - 1), step, S); /* S_(k-1) */
			VectorAdd(Qprev, step, R);
			VectorMA(R, jp, p, R);
			VectorMA(R, jq, q, R);
			VectorScale(S, 0.3f, Q);
			VectorMA(Q, 0.7f, R, Q);
			VectorSubtract(R, Qprev, d);
			VectorMA(Qprev, -0.06f, d, from);
		}

		l = FX_Line(from, Q, 7, seglen * 0.14f, 250, 250, 250, 250, 200, 1, false);

		if (l)
		{
			l->arate = -500;
		}

		if (branches && (FX_crand() % 5 == 0))
		{
			tip[0] = Q[0] + FX_flrand(-seglen, seglen);
			tip[1] = Q[1] + FX_flrand(-seglen, seglen);
			tip[2] = Q[2] + FX_flrand(-seglen, seglen);
			FX_Line(from, tip, 7, seglen * 0.14f, 250, 250, 250, 250, 500, 1, false);
		}
	}
}

void
FX_RecursiveBolt(const vec3_t a0, const vec3_t b, int depth, float prob, float width)
{
	vec3_t a, m, e, branch;
	fx_part_t *l;
	int i, d;

	VectorCopy(a0, a);

	while (depth > 0)
	{
		if (depth == 1)
		{
			VectorCopy(b, m);
		}
		else
		{
			for (i = 0; i < 3; i++)
			{
				m[i] = a[i] + (b[i] - a[i]) / depth;
			}
		}

		for (i = 0; i < 3; i++)
		{
			m[i] += FX_flrand(-90, 90);
			e[i] = a[i] + 1.1f * (m[i] - a[i]);
		}

		/* sprite `lit`, additive, white 250 (SPEC-AMEND-005 I5.7, I5.15) */
		l = FX_Line(a, e, 7, 10.0f * width, 250, 250, 250, 250, 500, 1, false);

		if (l)
		{
			l->arate = -500;
		}

		if (FX_flrand(0, 1) < prob)
		{
			for (i = 0; i < 3; i++)
			{
				branch[i] = b[i] + FX_flrand(-80, 80) * depth;
			}

			d = FX_irand(1, (int)(depth - 1));
			FX_RecursiveBolt(m, branch, d, FX_flrand(0, prob), FX_flrand(0.1f, width));
		}

		VectorCopy(m, a);
		depth--;
	}
}

/* ---- timer callbacks (SPEC-401 Timers) ------------------------------------------------ */

static void
FX_FireballPuffs(fx_part_t *t, float f, float smin, float smax, float size, int sprite,
		qboolean white, int blend)
{
	fx_puff_t pf;
	vec3_t o, d;
	fx_part_t *p;
	int i, c;

	for (i = 0; i < FX_irand(3, 6); i++)
	{
		FX_PuffInit(&pf);
		pf.sprite = sprite;
		pf.blend = blend;
		pf.life_ms = 500;
		VectorCopy(t->pos, o);
		o[2] += FX_flrand(12, 16);
		VectorSet(d, FX_flrand(-100, 100), FX_flrand(-100, 100), FX_flrand(-40, 80));
		VectorNormalize(d);
		VectorScale(d, (smin == smax) ? smin : FX_flrand(smin, smax), pf.vel);
		VectorScale(pf.vel, -2, pf.accel);
		pf.accel[2] += 300;
		pf.w = pf.h = (f + 1) * size;

		if (white)
		{
			pf.r = pf.g = pf.b = 250;
			pf.a = 150;
		}
		else
		{
			c = FX_irand(150, 250);
			pf.r = pf.g = pf.b = c;
			pf.a = 130;
		}

		pf.rot = C628;
		pf.avel = C628;
		pf.grow = 2 * pf.w;
		pf.arate = -2.0f * pf.a;
		p = FX_Puff(o, &pf);
		(void)p;
	}
}

static void
FX_CB_FireballA(fx_part_t *t, float f)
{
	if (f >= 0.5f)
	{
		FX_FireballPuffs(t, f, 1000, 1500, 32, 28, false, 0);
	}
}

static void
FX_CB_FireballB(fx_part_t *t, float f)
{
	if (f >= 0.5f)
	{
		FX_FireballPuffs(t, f, 400, 800, 32, 28, true, 1);
	}
}

static void
FX_CB_SmokeBallA(fx_part_t *t, float f)
{
	if (f > 0.5f)
	{
		FX_FireballPuffs(t, f, 600, 600, 16, 0, false, 0);
	}
}

static void
FX_CB_SmokeBallB(fx_part_t *t, float f)
{
	if (f > 0.5f)
	{
		FX_FireballPuffs(t, f, 600, 600, 16, 0, true, 1);
	}
}

static void
FX_CB_SmokeC(fx_part_t *t, float f)
{
	fx_puff_t pf;
	vec3_t o, r;

	(void)f;
	FX_RandVec(r, 20);
	VectorAdd(t->pos, r, o);
	FX_PuffInit(&pf);
	pf.sprite = 0;
	pf.blend = 2;
	pf.life_ms = 1000;
	pf.w = pf.h = 30;
	pf.r = pf.g = pf.b = 250;
	pf.a = 150;
	FX_RandVec(pf.vel, 160);
	VectorSet(pf.accel, 0, 0, 800);
	pf.avel = C628;
	pf.grow = 30;
	pf.arate = -150;
	FX_Puff(o, &pf);
}

static void
FX_CB_GroundSmoke(fx_part_t *t, float f)
{
	fx_puff_t pf;
	vec3_t o;
	int c;

	VectorCopy(t->pos, o);
	o[0] += FX_flrand(-10, 10);
	o[1] += FX_flrand(-10, 10);
	FX_PuffInit(&pf);
	pf.sprite = (FX_crand() % 2) ? 16 : 0;
	pf.blend = 1;
	c = FX_irand(120, 250);
	pf.r = pf.g = pf.b = c;
	pf.a = (int)(f * 200);
	pf.life_ms = (f + 1) * 500;
	pf.w = pf.h = 30;
	VectorSet(pf.vel, 0, 0, 50);
	VectorSet(pf.accel, 0, 0, 400);
	pf.avel = (float)(FX_crand() % 628) * 0.06f - 18.84f;
	pf.grow = -30.0f / ((f + 1) * 0.5f);
	FX_Puff(o, &pf);
}

static void
FX_CB_LimbSmoke(fx_part_t *t, float f)
{
	fx_puff_t pf;
	fx_part_t *p;

	FX_PuffInit(&pf);
	pf.sprite = (FX_crand() % 2) ? 16 : 0;
	pf.blend = 1;
	pf.life_ms = 800;
	pf.w = pf.h = FX_flrand(4, 6);
	pf.a = (int)(f * 170 + 50);
	FX_RandVec(pf.vel, 5);
	VectorSet(pf.accel, 0, 0, 160);
	pf.rot = C628;
	pf.avel = C628;
	pf.arate = -1.25f * pf.a;
	p = FX_Puff(t->pos, &pf);
	(void)p;
}

static void
FX_CB_OriginSmoke(fx_part_t *t, float f)
{
	fx_puff_t pf;
	int c;

	FX_PuffInit(&pf);
	pf.sprite = 0;
	pf.w = pf.h = 5;
	c = FX_irand(200, 250);
	pf.r = pf.g = pf.b = c;
	pf.a = (int)(f * 100);
	pf.life_ms = 1000;
	VectorSet(pf.accel, FX_flrand(-50, 50), FX_flrand(-50, 50), 200);
	pf.avel = (float)(FX_crand() % 628) * 0.06f - 18.84f;
	pf.grow = 30;
	pf.arate = -0.8333f * pf.a;
	FX_Puff(t->pos, &pf);
}

/* ---- burning debris trails (SPEC-401 Burning debris) ------------------------------------ */

static void
FX_FirePuff(fx_part_t *p, int sprite)
{
	fx_puff_t pf;
	float k = FX_flrand(0.85f, 1.15f);

	FX_PuffInit(&pf);
	pf.sprite = sprite;
	pf.blend = 1;
	pf.life_ms = 500;
	pf.w = 2 * k * p->d1;
	pf.h = 2 * k * p->d2;
	pf.r = FX_irand(180, 250);
	pf.g = FX_irand(pf.r - 80, pf.r);
	pf.b = FX_irand(pf.g - 80, pf.g);
	pf.a = 250;
	VectorScale(p->vel, 0.35f, pf.vel);
	pf.rot = FX_RandRot(); /* every trail puff (SPEC-AMEND-005 I5.15) */
	pf.avel = FX_flrand(-3, 3);
	pf.grow = -8;
	pf.arate = -500;
	FX_Puff(p->pos, &pf);
}

static void
FX_TrailFireSmoke(fx_part_t *p)
{
	fx_puff_t pf;
	float k;
	int idx = (int)(p - fx_parts);

	FX_FirePuff(p, FX_irand(49, 50));
	p = &fx_parts[idx];
	k = FX_flrand(0.85f, 1.15f);
	FX_PuffInit(&pf);
	pf.sprite = 52;
	pf.blend = 2;
	pf.life_ms = 1000;
	pf.w = k * p->d1;
	pf.h = k * p->d2;
	pf.r = pf.g = pf.b = 255;
	pf.a = 250;
	VectorSet(pf.accel, 0, 0, 50);
	pf.rot = FX_RandRot();
	pf.avel = FX_flrand(-3, 3);
	pf.grow = 20;
	pf.arate = -250;
	FX_Puff(p->pos, &pf);
}

/* fire trail (0.05 s): the fire puff with the parent's dimensions swapped and no factor 2k
   (a factor rand(2.5, 3.0) is drawn and not used; SPEC-AMEND-005 I5.3) */
static void
FX_TrailFire(fx_part_t *p)
{
	fx_puff_t pf;

	(void)FX_flrand(2.5f, 3.0f);
	FX_PuffInit(&pf);
	pf.sprite = 47;
	pf.blend = 1;
	pf.life_ms = 500;
	pf.h = p->d2;
	pf.w = p->d1;
	pf.r = FX_irand(180, 250);
	pf.g = FX_irand(pf.r - 80, pf.r);
	pf.b = FX_irand(pf.g - 80, pf.g);
	pf.a = 250;
	VectorScale(p->vel, 0.35f, pf.vel);
	pf.rot = FX_RandRot();
	pf.avel = FX_flrand(-3, 3);
	pf.grow = -8;
	pf.arate = -500;
	FX_Puff(p->pos, &pf);
}

static void
FX_TrailColouredSmoke(fx_part_t *p)
{
	fx_puff_t pf;
	float k = FX_flrand(0.85f, 1.15f);

	FX_PuffInit(&pf);
	pf.sprite = FX_irand(51, 53);
	pf.blend = 1;
	pf.life_ms = 1000;
	pf.w = 2 * k * p->d1;
	pf.h = 2 * k * p->d2;
	pf.r = FX_irand(200, 255);
	pf.g = 255;
	pf.b = FX_irand(225, 255);
	pf.a = 255;
	pf.rot = FX_RandRot();
	pf.avel = FX_flrand(-3, 3);
	pf.grow = -4;
	pf.arate = -255;
	FX_Puff(p->pos, &pf);
}

static fx_part_t *
FX_BurningBit(const vec3_t org, float size, float life_ms, const vec3_t vel, float gravity,
		float grow, float arate, fx_trail_cb_t trail, float interval)
{
	fx_puff_t pf;
	fx_part_t *p;

	FX_PuffInit(&pf);
	pf.sprite = 10;
	pf.blend = 1;
	pf.life_ms = life_ms;
	pf.w = pf.h = size;
	pf.rot = FX_RandRot(); /* SPEC-AMEND-005 I5.15; colour stays (255, 255, 255, 255) */
	VectorCopy(vel, pf.vel);
	VectorSet(pf.accel, 0, 0, gravity);
	pf.avel = FX_flrand(-3, 3);
	pf.grow = grow;
	pf.arate = arate;
	p = FX_Puff(org, &pf);

	if (p)
	{
		p->trailcb = trail;
		p->trailint = interval;
		p->trailtime = fx_now;
	}

	return p;
}

/* ---- shared effects used by impacts too ------------------------------------------------ */

void
FX_SodaCans(const vec3_t org, const vec3_t dir, int speed, int count)
{
	vec3_t v, av, zero = {0, 0, 0};
	int i, k;
	float s;

	for (i = 0; i < count; i++)
	{
		s = FX_flrand(0.32f, 1.6f);

		for (k = 0; k < 3; k++)
		{
			v[k] = (float)FX_irand(-32, 32) + s * (dir[k] + FX_flrand(-0.1f, 0.1f)) * speed;
		}

		VectorSet(av, 0, (float)FX_irand(-900, 900), 0);
		FX_AddDebris(21, org, zero, v, av, -300, 5);
	}

	FX_TraceLog("  soda cans x%d\n", count);
}

void
FX_SparkShower(const vec3_t org, const vec3_t dir)
{
	int i, n = FX_irand(5, 11);

	fx_part_t *p;

	for (i = 0; i < n - 1; i++)
	{
		p = FX_SparkParticle(org, dir, 32, 160, 1, 250, -800, 10, 0, 100, 100);

		if (p)
		{
			p->rgb[2] = 120; /* (100, 100, 120) */
		}
	}

	FX_TraceLog("  spark shower x%d\n", n - 1);
}

void
FX_BlowingPaper(const vec3_t org, int count)
{
	vec3_t o, ang, v, av;
	fx_debris_t *d;
	float q;
	int i;

	if (!FX_LimitPaper())
	{
		FX_TraceLog("  blowing paper ignored (300 ms limit)\n");
		return;
	}

	if (!count)
	{
		count = FX_irand(5, 10);
	}

	for (i = 0; i < count; i++)
	{
		VectorSet(o, org[0] + FX_flrand(-4, 4), org[1] + FX_flrand(-4, 4), org[2] + FX_flrand(1, 5));
		VectorSet(ang, FX_flrand(40, 60), FX_flrand(0, 359), 0);
		q = FX_flrand(0, 70);
		VectorSet(v, cosf(FX_DEG2RAD(ang[1])) * q, sinf(FX_DEG2RAD(ang[1])) * q, FX_flrand(180, 240));
		VectorSet(av, -100, 0, 0);
		d = FX_AddDebris(22, o, ang, v, av, -800, 1);

		if (d)
		{
			d->leaf = true;
		}
	}

	FX_TraceLog("  blowing paper x%d\n", count);
}

/* ---- temp-entity types -------------------------------------------------------------- */

static void
TE_FloorDecal(int type)
{
	vec3_t org, end;
	trace_t tr;
	int hit;
	fx_impact_t im;

	FX_ReadPos(org);
	(void)MSG_ReadLong(&net_message);
	VectorCopy(org, end);
	end[2] -= 256;
	FX_Trace(org, NULL, NULL, end, FXTR_BRUSHES, &tr, &hit);

	if (tr.fraction >= 1.0f)
	{
		return;
	}

	memset(&im, 0, sizeof(im));
	im.kind = hit ? 2 : 0;
	im.ent = hit ? hit : -1;
	VectorCopy(tr.endpos, im.point);
	VectorCopy(tr.plane.normal, im.normal);
	im.material = FX_TraceMaterial(&tr);
	im.surfflags = tr.surface ? (tr.surface->flags & 0xFFFFFF) : 0;
	im.leaf = CM_PointLeafnum(im.point);

	switch (type)
	{
		case 4:
			FX_AddDecal(&im, FX_SpriteName(4), 100, 0, 0, 250, 2, -1, 0, 0, 0);
			break;
		case 15:
			FX_AddDecal(&im, FX_SpriteName(26), 255, 250, 250, 255, 6, -1, 0, 0, 0);
			break;
		case 17:
			(void)FX_irand(200, 240);
			break;
		case 28:
			FX_AddDecal(&im, FX_SpriteName(32), 255, 250, 250, 255, 2, -1, 0, 0, 0);
			break;
		case 53:
			FX_AddDecal(&im, FX_SpriteName(56), 170, 190, 250, 200, 2, -1, 0, 0, 0);
			break;
	}
}

static void
TE_Spray(void)
{
	vec3_t org, dir, fwd, right, up;
	fx_puff_t pf;
	int i, count, size;

	FX_ReadPos(org);
	dir[0] = (float)(MSG_ReadByte(&net_message) - 128);
	dir[1] = (float)(MSG_ReadByte(&net_message) - 128);
	dir[2] = (float)(MSG_ReadByte(&net_message) - 128);
	size = MSG_ReadByte(&net_message);
	FX_AxesFromDir(dir, fwd, right, up);
	count = (int)ceilf(10.0f * FX_DetailFactor() + 1.0f);

	for (i = 0; i < count; i++)
	{
		FX_PuffInit(&pf);
		pf.sprite = 0;
		pf.blend = 1;
		pf.life_ms = 750;
		pf.w = pf.h = (float)(size / 3);
		pf.r = 220;
		pf.g = 240;
		pf.b = 255;
		pf.a = 100;
		pf.rot = FX_flrand(0, (float)M_PI / 2.0f);
		VectorScale(fwd, (float)(FX_crand() % 50 + 90 * i), pf.vel);
		VectorMA(pf.vel, (float)(FX_crand() % 240 - 160), right, pf.vel);
		VectorMA(pf.vel, (float)(FX_crand() % 240 - 160), up, pf.vel);
		VectorScale(pf.vel, size * 0.01f, pf.vel);
		VectorSet(pf.accel, 0, 0, 600);
		pf.avel = FX_flrand(0.5f, 0.8f);
		pf.grow = (FX_flrand(1, 2) + 1.5f) * 1.3333f;
		pf.arate = 133.33f;
		FX_Puff(org, &pf);
	}

	FX_TraceLog("  spray x%d size %d\n", count, size);
}

static void
TE_WeaponImpact(int type)
{
	fx_impact_t im;
	vec3_t dir;
	int power, style, debris;

	FX_ReadImpact(&im);
	FX_ReadScaledPos(dir, 0.001f);
	power = MSG_ReadByte(&net_message);
	style = MSG_ReadByte(&net_message);
	debris = MSG_ReadByte(&net_message);

	FX_TraceLog("TE %d impact kind %d material %d at (%.0f %.0f %.0f) power %d style %d debris %d\n",
			type, im.kind, im.material, im.point[0], im.point[1], im.point[2], power, style, debris);

	/* SPEC-AMEND-005 I5.4: 1.07f compares the style byte (not the material) with 42 */
	if (style > 42)
	{
		Com_Printf("Surface out of range!!!\n");
		return;
	}

	if (style > 2)
	{
		style = 0;
	}

	/* the same impact preparation for types 6 and 7 (SPEC-AMEND-005 I5.6) */
	im.markmode = 4;
	im.power = (style == 1) ? (float)power : 0;
	im.hole = -1;

	if (im.material >= 0 && im.material <= 42)
	{
		switch (style)
		{
			case 1:
				im.hole = fx_materials[im.material].hole;
				break;
			case 2:
				im.hole = 34;
				break;
			default:
				im.hole = (im.kind == 0) ? fx_materials[im.material].hole : -1;
				break;
		}
	}
	else if (style == 2)
	{
		im.hole = 34;
	}

	/* style 1 with the debris flag: exactly one chunk with scale 25, before the type test,
	   so type 7 throws it too (I5.5); the impact-chunk rules decide (chunk entry, 200 ms) */
	if ((style == 1) && debris && (im.material >= 0) && (im.material <= 42))
	{
		FX_ImpactChunks(&im, im.material, 1, 25, -1);
	}

	if (im.material < 0)
	{
		return; /* no material (kind 1): nothing, no message */
	}

	if (im.material > 42)
	{
		/* only kinds 2 and 3 can carry such a material (payload byte) */
		if (type == 7)
		{
			Com_Printf("Surface out of range!!!\n");
		}

		return;
	}

	if (type == 7)
	{
		/* 6 chunks with the material-18 model (quirk), then mark and sound with the
		   prepared power and hole, then the material effect with power 12, no direction */
		FX_ImpactChunks(&im, im.material, 6, 300, 14);
		FX_MarkAndSound(&im, 0);
		VectorClear(dir);
		FX_MaterialEffect(&im, dir, 12);
		return;
	}

	if (!(im.surfflags & (SURF_SKY | SURF_NODRAW)))
	{
		FX_MarkAndSound(&im, 0);
		FX_MaterialEffect(&im, dir, power);
	}
}

static void
TE_ElectricBurst(void)
{
	vec3_t org, end;
	trace_t tr;
	int i, hit;
	fx_part_t *f;
	int r = FX_irand(120, 170), g = FX_irand(150, 200), b = FX_irand(220, 250);

	FX_ReadPos(org);
	f = FX_Flash(org, 20, r, g, b, 250);

	if (f)
	{
		f->r1 = f->r2 = -40;
	}

	CL_SoF_AddLight(0, org, 128, 128, 160, 128, 500, 128);

	for (i = 0; i < 12; i++)
	{
		VectorSet(end, org[0] + FX_flrand(-256, 256), org[1] + FX_flrand(-256, 256),
				org[2] + FX_flrand(-256, 0));
		FX_Trace(org, NULL, NULL, end, FXTR_BRUSHES, &tr, &hit);

		if (tr.fraction >= 0.2f)
		{
			FX_ElectricArc(org, tr.endpos, (float)(int)(tr.fraction * 8), true);

			if (tr.fraction < 0.9f)
			{
				f = FX_Flash(tr.endpos, 10, r, g, b, 250);

				if (f)
				{
					f->r1 = f->r2 = -20;
				}
			}
		}
	}
}

static void
TE_GroundFlash(void)
{
	fx_impact_t im;
	fx_puff_t pf;

	FX_ReadImpact(&im);
	CL_SoF_AddLight(0, im.point, 160, 160, 128, 128, 150, 50);
	FX_PuffInit(&pf);
	pf.sprite = 25;
	pf.blend = 1;
	pf.life_ms = 165;
	pf.w = pf.h = 10;
	pf.r = FX_irand(120, 170);
	pf.g = FX_irand(70, 130);
	pf.b = FX_irand(70, 130);
	pf.a = 255;
	pf.rot = FX_RandRot();
	pf.avel = FX_flrand(-18.84f, 18.84f);
	pf.grow = 600;
	pf.arate = -133.33f;
	FX_Puff(im.point, &pf);
}

static void
TE_Lightning(void)
{
	vec3_t a, b;
	float x, y, prob;
	int i, n = FX_irand(1, 3);

	for (i = 0; i < n; i++)
	{
		x = cl.refdef.vieworg[0] + ((FX_crand() % 2) ? 1 : -1) * FX_flrand(1000, 4000);
		y = cl.refdef.vieworg[1] + ((FX_crand() % 2) ? 1 : -1) * FX_flrand(1000, 4000);
		VectorSet(a, x, y, cl.refdef.vieworg[2] + FX_flrand(2800, 4000));
		VectorSet(b, x, y, cl.refdef.vieworg[2] + FX_flrand(-1200, -500));
		prob = FX_flrand(0.6f, 1.0f) - 0.2f * n;
		FX_RecursiveBolt(a, b, 10, prob, 2);
	}

	CL_SoF_LightningFlash(1.0f, 1.0f, 1.8f, 100);
	FX_TraceLog("  lightning: %d bolts\n", n);
}

static const int fx_chunk_model_base[35] = {
	4, 0, 0, 0, 0, 0, 0, 1, 4, 4, 4, 4, 7, 7, 7, 7, 7, 15, 15, 15, 15,
	11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 7, 11, 7
};

static const char *fx_chunk_skin[35] = {
	"light",
	"brwn_lt", "brwn_dk", "brwn_lt", "grey_lt", "grey_dk", "red",
	"glass",
	"light", "dark", "rusty", "shiny",
	"brwn_lt", "brwn_lt", "brwn_dk", "grey_dk", "grey_dk",
	"brwn_lt", "brwn_dk", "grey_dk", "grey_lt",
	"black", "brown", "dbrown", "lbrown", "grey", "dgrey", "lgrey", "green", "orange_cone", "red",
	"white",
	"flesh", "straw", "snow"
};

static void
TE_Debris(void)
{
	vec3_t org, dir, o, v, av, zero = {0, 0, 0}, d;
	int count, size, ctype, ex, ey, ez, mat, i, k, model, cclass, snd;
	float grav, vol = 0.9f, att = 1.0f;
	fx_debris_t *dbr;
	fx_puff_t pf;

	FX_ReadPos(org);
	FX_ReadDir(dir);
	count = MSG_ReadByte(&net_message);
	size = MSG_ReadByte(&net_message);
	ctype = MSG_ReadByte(&net_message);
	ex = (int)(0.85f * MSG_ReadByte(&net_message));
	ey = (int)(0.85f * MSG_ReadByte(&net_message));
	ez = (int)(0.85f * MSG_ReadByte(&net_message));
	mat = MSG_ReadByte(&net_message);

	if ((ctype < 0) || (ctype > 34))
	{
		ctype = 8;
	}

	if (ctype == 0)
	{
		ctype = 8;
	}

	if (size < 1)
	{
		size = 1;
	}

	model = fx_chunk_model_base[ctype];

	if ((ctype >= 7) && (model != 0))
	{
		/* Size classes 1..N map onto consecutive debris model indices.
		   Rock (base 7) and wall (base 11) include TINY as class 4
		   (SPEC-401 / SPEC-AMEND-014 R14.10: flesh TE 18 uses size class 4 →
		   chunks_rock ROCK_TINY). Clamping class 4 to 3 made flesh bits use
		   ROCK_LG (half-person scale). Other series stay at 1..3. */
		int maxsz = 3;

		if ((model == 7) || (model == 11))
		{
			maxsz = 4;
		}

		if (size > maxsz)
		{
			size = maxsz;
		}

		model += size - 1;
	}
	else if (size > 3)
	{
		size = 3;
	}

	/* Chunk type 32 (flesh): rock meshes + flesh.tga. A SINGLE part of
	   ROCK_LG is 24-40 quake units (~half a player); ROCK_MED ~20.
	   Owner still reported half-person meat after size-4->TINY alone, so any
	   flesh TE 18 that picked LG/MED (size class 2/3 from retail or other
	   emitters) stays huge. Force ROCK_TINY for all flesh debris. */
	if (ctype == 32)
	{
		model = 10;
	}

	cclass = ((ctype >= 12) && (ctype <= 16)) ? 5 + size : 6;

	FX_TraceLog("TE 18 debris x%d size %d chunk type %d material %d model %d (%s) at (%.1f %.1f %.1f) dir (%.2f %.2f %.2f) "
			"extent (%d %d %d)\n", count, size, ctype, mat, model,
			((model >= 0) && (model < FX_NUM_DEBRIS_MODELS)) ? fx_debris_models[model].file : "?",
			org[0], org[1], org[2], dir[0], dir[1], dir[2],
			ex, ey, ez);

	for (i = 0; i < count; i++)
	{
		o[0] = org[0] + (float)FX_irand(-ex, ex);
		o[1] = org[1] + (float)FX_irand(-ey, ey);
		o[2] = org[2] + (float)FX_irand(-ez, ez);

		for (k = 0; k < 3; k++)
		{
			av[k] = (float)FX_irand(-900, 900);
		}

		if (mat == 35)
		{
			VectorSet(v, (float)FX_irand(-100, 100), (float)FX_irand(-100, 100),
					(float)FX_irand(-10, 100));
			grav = -400;
		}
		else if (mat == 37)
		{
			for (k = 0; k < 3; k++)
			{
				d[k] = dir[k] + FX_flrand(-0.05f, 0.05f);
			}

			VectorScale(d, FX_flrand(50, 100), v);
			v[2] += FX_flrand(100, 300);
			grav = -400;
		}
		else
		{
			for (k = 0; k < 3; k++)
			{
				v[k] = (float)FX_irand(-300, 300);
			}

			grav = -800;
		}

		dbr = FX_AddDebris(model, o, zero, v, av, grav, cclass);

		if (dbr)
		{
			dbr->skin = fx_chunk_skin[ctype];
			dbr->settle = true;
			dbr->part = FX_irand(0, FX_DebrisParts(model)); /* inclusive: n is invisible */

			if (fx_trace && (fx_trace->value >= 2))
			{
				FX_TraceLog("  chunk %d model %d part %d at (%.1f %.1f %.1f) v (%.0f %.0f %.0f)\n", i, model,
						dbr->part, o[0], o[1], o[2], v[0], v[1], v[2]);
			}
		}

		if ((i % 5 == 0) && (ctype != 32))
		{
			FX_PuffInit(&pf);
			pf.sprite = 0;
			pf.lit = true;
			pf.life_ms = 1200;
			pf.w = pf.h = 10;
			pf.r = pf.g = pf.b = 170;
			pf.a = 200;
			pf.rot = C628;
			pf.avel = C628;
			VectorSet(pf.vel, 0, 0, 30);
			VectorSet(pf.accel, 0, 0, 100);
			pf.grow = 0.6f;
			pf.arate = -333;
			FX_Puff(o, &pf);
		}
	}

	if ((mat == 37) || (mat == 38))
	{
		snd = (size == 3) ? 82 : ((size == 2) ? 83 : 84);
		vol = 0.6f;
	}
	else if ((mat >= 17) && (mat <= 24))
	{
		snd = 86;
	}
	else if ((mat >= 14) && (mat <= 16))
	{
		snd = 87;
	}
	else if ((mat == 28) || (mat == 42) || ((mat >= 29) && (mat <= 32)))
	{
		snd = 85;
	}
	else if (mat == 35)
	{
		snd = FX_irand(45, 47);
		att = 3;
	}
	else
	{
		snd = 86;
	}

	FX_PlaySound(snd, org, vol, att, -1, 5, 0);
}

static void
TE_ExplosionDebris(void)
{
	fx_impact_t im;
	vec3_t n, r, u, v, av, zero = {0, 0, 0};
	fx_puff_t pf;
	fx_part_t *p;
	fx_debris_t *d;
	int i, k, count;
	float L;

	FX_ReadImpact(&im);
	VectorCopy(im.normal, n);
	FX_Perpendicular(n, r);
	CrossProduct(n, r, u);

	count = FX_irand(3, 4);

	for (i = 0; i < count; i++)
	{
		VectorScale(n, FX_flrand(0, 450), v);
		VectorMA(v, FX_flrand(-300, 300), r, v);
		VectorMA(v, FX_flrand(-300, 300), u, v);
		FX_BurningBit(im.point, 12, (float)FX_irand(600, 800), v, -600, FX_flrand(-14, -11),
				FX_flrand(-255, -175), FX_TrailFireSmoke, 0.075f);
	}

	count = FX_irand(2, 3);

	for (i = 0; i < count; i++)
	{
		VectorScale(n, FX_flrand(0, 600), v);
		VectorMA(v, FX_flrand(-300, 300), r, v);
		VectorMA(v, FX_flrand(-300, 300), u, v);
		FX_BurningBit(im.point, 8, (float)FX_irand(800, 1000), v, -600, -7.5f, -255,
				FX_TrailFire, 0.05f);
	}

	for (i = 0; i < 3; i++)
	{
		FX_PuffInit(&pf);
		FX_Orange(&pf);
		pf.sprite = 48;
		pf.blend = 1;
		L = (float)FX_irand(300, 400);
		pf.life_ms = L;
		pf.w = pf.h = 6;
		VectorScale(n, FX_flrand(0, 100), pf.vel);
		VectorMA(pf.vel, FX_flrand(-100, 100), r, pf.vel);
		VectorMA(pf.vel, FX_flrand(-100, 100), u, pf.vel);
		pf.rot = FX_RandRot();
		pf.avel = FX_flrand(-3, 3);
		pf.grow = 30.0f / (L / 1000.0f);
		p = FX_Puff(im.point, &pf);
		FX_FadeOverLife(p, L);
	}

	for (i = 0; i < 10; i++)
	{
		VectorSet(v, (float)FX_irand(-300, 300), (float)FX_irand(-300, 300),
				(float)FX_irand(600, 2000));

		for (k = 0; k < 3; k++)
		{
			av[k] = (float)FX_irand(-900, 900);
		}

		d = FX_AddDebris(7, im.point, zero, v, av, -1600, 6);

		if (d)
		{
			d->skin = fx_chunk_skin[16];
			d->settle = true;
			d->part = FX_irand(0, FX_DebrisParts(7));
		}
	}

	FX_TraceLog("TE 19 explosion debris at (%.0f %.0f %.0f)\n", im.point[0], im.point[1],
			im.point[2]);
}

static void
TE_FlashGrenade(void)
{
	vec3_t org, end, v;
	trace_t tr;
	int hit, i;
	fx_part_t *f;

	FX_ReadPos(org);
	VectorCopy(org, end);
	end[2] -= 16;
	FX_Trace(org, NULL, NULL, end, FXTR_BRUSHES, &tr, &hit);

	if (tr.fraction < 0.99f)
	{
		VectorCopy(tr.endpos, org);
	}

	for (i = 0; i < 2; i++)
	{
		f = FX_Flash(org, 360, 250, 250, 250, 250);

		if (f)
		{
			f->avel = FX_flrand(-1, 1);
			f->r1 = f->r2 = -640;

			if (i == 1)
			{
				f->q.flags |= AQ_ORIENTED;
				VectorSet(f->q.normal, 0, 0, 1);
			}
		}
	}

	for (i = 0; i < 15; i++)
	{
		f = FX_Flash(org, 32, 250, 250, 250, 250); /* "white" = 250 (I5.15) */

		if (f)
		{
			FX_RandVec(v, 200);
			VectorCopy(v, f->vel);
			f->avel = FX_flrand(-1, 1);
			f->r1 = f->r2 = -64;
		}
	}

	CL_SoF_AddLight(0, org, 230, 230, 230, 192, 500, 100);
	FX_PlaySound(54, org, 0.6f, 0.3f, 0, 0, 0);
}

static void
TE_ShockPuff(void)
{
	vec3_t org;
	fx_puff_t pf;

	FX_ReadPos(org);
	org[2] += 4;
	FX_PuffInit(&pf);
	pf.sprite = 21;
	pf.blend = 1;
	pf.life_ms = 200;
	pf.w = pf.h = 6;
	pf.r = pf.g = pf.b = pf.a = 250; /* "white" = 250, random rotation (I5.15) */
	pf.rot = FX_RandRot();
	pf.flat = true;
	pf.avel = FX_flrand(-1, 1);
	pf.grow = 320;
	pf.arate = -250;
	FX_Puff(org, &pf);
}

static void
TE_Thud(void)
{
	vec3_t org;
	int mat;

	FX_ReadPos(org);
	mat = MSG_ReadByte(&net_message);

	if (mat >= 43)
	{
		Com_Printf("Surface out of range!!!\n");
		return;
	}

	FX_PlaySound(fx_materials[mat].landing, org, 1.0f, 1.0f, -1, 0, 0);
	FX_PlaySound(101, org, 1.0f, 1.0f, -1, 0, 0);
}

static void
FX_PelletImpact(const vec3_t start, const vec3_t dir, float range, int hole, float power,
		const vec3_t effdir, int effpower, float vol)
{
	vec3_t end;
	trace_t tr;
	int hit;
	fx_impact_t im;

	VectorMA(start, range, dir, end);
	FX_Trace(start, NULL, NULL, end, FXTR_BRUSHES, &tr, &hit);

	if (tr.fraction >= 1.0f)
	{
		return;
	}

	memset(&im, 0, sizeof(im));
	im.kind = hit ? 2 : 0;
	im.ent = hit ? hit : -1;
	VectorCopy(tr.endpos, im.point);
	VectorCopy(tr.plane.normal, im.normal);
	im.material = FX_TraceMaterial(&tr);
	im.surfflags = tr.surface ? (tr.surface->flags & 0xFFFFFF) : 0;
	im.leaf = CM_PointLeafnum(im.point);
	im.markmode = 4;
	im.power = power;
	im.hole = (hole == -2) ? ((im.material < FX_NUM_MATERIALS) ? fx_materials[im.material].hole : -1) :
		hole;

	if (im.material > 42)
	{
		return;
	}

	FX_MarkAndSound(&im, vol);
	FX_MaterialEffect(&im, effdir, effpower);
}

static void
TE_ShotgunPellets(void)
{
	vec3_t org, dir, d;
	int n, i, k, hits = 0, m;

	FX_ReadPos(org);
	FX_ReadPos(dir); /* not scaled */
	n = MSG_ReadByte(&net_message);

	for (i = 0; i < n; i++)
	{
		(void)MSG_ReadShort(&net_message);
		hits += MSG_ReadByte(&net_message);
	}

	m = (12 - hits) & 0xFF; /* byte arithmetic */
	FX_TraceLog("TE 24 shotgun: %d pellets not accounted for\n", m);

	for (i = 0; i < m; i++)
	{
		for (k = 0; k < 3; k++)
		{
			d[k] = dir[k] + FX_flrand(-0.13f, 0.13f);
		}

		FX_PelletImpact(org, d, 1024, -1, FX_flrand(1.5f, 3.5f), d, (int)FX_flrand(2, 4), 0);
	}
}

static void
TE_Bubbles(void)
{
	vec3_t start, end, d, step, o;
	fx_puff_t pf;
	int i, n;

	FX_ReadPos(start);
	FX_ReadPos(end);
	VectorSubtract(end, start, d);
	n = (int)(VectorLength(d) / 16);

	if (n <= 0)
	{
		return;
	}

	VectorScale(d, 1.0f / n, step);

	for (i = 0; i < n; i++)
	{
		VectorMA(start, (float)i, step, o);
		FX_PuffInit(&pf);
		pf.sprite = 31;
		pf.life_ms = 600;
		pf.w = pf.h = FX_flrand(0.6f, 1.2f);
		FX_RandVec(pf.vel, 30);
		VectorSet(pf.accel, 0, 0, 75);
		pf.arate = -1.6667f * pf.a;
		FX_Puff(o, &pf);
	}
}

static void
TE_SmokeBurst(void)
{
	vec3_t org;
	fx_puff_t pf;
	int i, b;

	FX_ReadPos(org);
	b = MSG_ReadByte(&net_message);

	for (i = 0; i < 12; i++)
	{
		FX_PuffInit(&pf);
		pf.sprite = 0;
		pf.blend = 2;
		pf.life_ms = 800;
		pf.w = pf.h = 8;
		pf.r = pf.g = pf.b = 250;
		pf.a = 160;
		pf.rot = FX_flrand(0, (float)M_PI);
		FX_RandVec(pf.vel, 60);
		VectorSet(pf.accel, 0, 0, 100);
		pf.avel = FX_flrand(-1, 1);
		pf.grow = b * 1.25f;
		FX_Puff(org, &pf);
	}
}

static void
TE_DebugLine(void)
{
	vec3_t start, end;
	unsigned col;
	int dur;
	fx_part_t *l;

	FX_ReadPos(start);
	FX_ReadPos(end);
	col = (unsigned)MSG_ReadLong(&net_message);
	dur = MSG_ReadLong(&net_message);
	l = FX_Line(start, end, 14, 1, col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF,
			(col >> 24) & 0xFF, dur * 500.0f, 0, false);

	if (l && dur)
	{
		l->r1 = l->r2 = 20.0f / dur;
		FX_FadeOverLife(l, dur * 500.0f);
	}
}

static void
TE_ImpactDecal(int type)
{
	fx_impact_t im;

	FX_ReadImpact(&im);

	if (type == 3)
	{
		FX_AddDecal(&im, FX_SpriteName(FX_irand(1, 3)), FX_irand(80, 160), 80, 80,
				(float)FX_irand(175, 250), FX_flrand(8, 16), -1, 0, 0, 0);
	}
	else if (type == 29)
	{
		FX_AddDecal(&im, FX_SpriteName(29), 250, 250, 250, 144, (float)FX_irand(16, 64), -1, 0, 0, 0);
	}
	else
	{
		FX_AddDecal(&im, FX_SpriteName(32), 0, 0, 0, 255, 4, -1, 0, 0, 0);
	}
}

static void
TE_LitSparks(void)
{
	vec3_t org, dir;
	int i, n, k, c[3];
	float L = 1.0f;
	fx_puff_t pf;
	vec3_t o, light;

	FX_ReadPos(org);
	FX_ReadDir(dir);

	/* brightness = length of the world light colour (renderer slot 0xa4) */
	if (FX_LightPoint(org, light))
	{
		L = VectorLength(light);
	}
	n = FX_irand(5, 11);

	for (i = 0; i < n - 1; i++)
	{
		FX_PuffInit(&pf);

		for (k = 0; k < 3; k++)
		{
			o[k] = org[k] + (float)(FX_crand() % 4 - 1);
			pf.vel[k] = (dir[k] + FX_flrand(-0.1f, 0.1f)) * (float)FX_irand(32, 50) +
				(float)FX_irand(-32, 32);
			c[k] = ((int)(FX_irand(-1, 250) * L)) & 0xFF;
		}

		VectorSet(pf.accel, 0, 0, -200);
		pf.sprite = 10;
		pf.life_ms = 1200;
		pf.w = pf.h = 1;
		pf.r = c[0];
		pf.g = c[1];
		pf.b = c[2];
		pf.a = 250;
		pf.grow = -0.8333f;
		FX_Puff(o, &pf);
	}
}

static void
TE_KnifeImpact(void)
{
	vec3_t org, n, v, e;
	fx_part_t *f, *l;
	int i, k;

	FX_ReadPos(org);
	FX_ReadDir(n);
	(void)MSG_ReadByte(&net_message);
	FX_PlaySound(69, org, 0.6f, 1.0f, 0, 0, 0);
	f = FX_Flash(org, 5, FX_irand(120, 170), FX_irand(150, 200), FX_irand(220, 250), 250);

	if (f)
	{
		FX_SetLife(f, 0.2f);
		f->avel = FX_flrand(-18.84f, 18.84f);
		f->r1 = f->r2 = -25;
	}

	for (i = 0; i < 8; i++)
	{
		for (k = 0; k < 3; k++)
		{
			v[k] = (n[k] + FX_flrand(-0.25f, 0.25f)) * 600;
		}

		VectorMA(org, 0.025f, v, e);
		l = FX_Line(org, e, 14, 1, 100, 100, 250, 250, 200, 1, false);

		if (l)
		{
			/* start velocity v, end velocity 1.5 v, no acceleration (SPEC-AMEND-005,
			   open-question answer on the streak lines) */
			VectorCopy(v, l->vel);
			VectorScale(v, 1.5f, l->endvel);
			l->r1 = l->r2 = -3;
			l->arate = -5.0f * l->alpha;
		}
	}
}

static void
TE_ScatterExplosion(void)
{
	vec3_t org, start, end, o, d;
	fx_puff_t pf;
	fx_part_t *p;
	int i;
	float L;

	FX_ReadPos(org);
	(void)MSG_ReadByte(&net_message);
	VectorCopy(org, start);
	start[2] += 16;

	for (i = 0; i < 32; i++)
	{
		VectorSet(end, org[0] + FX_flrand(-128, 128), org[1] + FX_flrand(-128, 128),
				org[2] + FX_flrand(-20, 300));
		VectorSubtract(end, start, d);
		{
			float len = VectorNormalize(d);
			trace_t tr;
			int hit;

			FX_Trace(start, NULL, NULL, end, FXTR_BRUSHES, &tr, &hit);

			if (tr.fraction < 0.99f)
			{
				FX_PelletImpact(start, d, len, -2, FX_flrand(1.5f, 3.5f), d, 4, 0);
			}
		}
	}

	for (i = 0; i < 4; i++)
	{
		FX_PuffInit(&pf);
		FX_Orange(&pf);
		pf.sprite = 28;
		pf.blend = 1;
		L = (float)FX_irand(100, 200);
		pf.life_ms = L;
		pf.w = pf.h = 6;
		FX_RandVec(d, 10);
		VectorAdd(org, d, o);
		FX_RandVec(pf.vel, 120);
		pf.rot = FX_RandRot(); /* I5.15 */
		pf.avel = FX_flrand(-3, 3);
		pf.grow = 30.0f / (L / 1000.0f);
		p = FX_Puff(o, &pf);
		FX_FadeOverLife(p, L);
	}

	for (i = 0; i < 12; i++)
	{
		FX_PuffInit(&pf);
		pf.sprite = 0;
		pf.life_ms = 400;
		pf.w = pf.h = 12;
		pf.r = pf.g = pf.b = 120;
		pf.a = 160;
		pf.rot = FX_flrand(0, (float)M_PI);
		FX_RandVec(pf.vel, 60);
		pf.vel[2] = 0.6f * (float)fabs(pf.vel[2]);
		VectorSet(pf.accel, 0, 0, 100);
		pf.avel = FX_flrand(-1, 1);
		pf.grow = -10;
		FX_Puff(org, &pf);
	}

	for (i = 0; i < 6; i++)
	{
		FX_RandVec(d, 8);
		VectorAdd(org, d, o);
		p = FX_Flash(o, 12, 250, 250, 250, 250); /* "white" = 250 (I5.15) */

		if (p)
		{
			p->avel = FX_flrand(-1, 1);
			p->arate = -1250;
		}
	}
}

static void
TE_ShotgunBlast(void)
{
	vec3_t org, dir, d;
	int i;

	FX_ReadPos(org);
	FX_ReadScaledPos(dir, 1.0f / 32.0f);

	for (i = 0; i < 10; i++)
	{
		d[0] = dir[0] + FX_flrand(-0.15f, 0.15f);
		d[1] = dir[1] + FX_flrand(-0.15f, 0.15f);
		d[2] = dir[2] + FX_flrand(-0.08f, 0.08f);
		FX_PelletImpact(org, d, FX_flrand(256, 640), 13, i ? FX_flrand(0.5f, 1.5f) : 4.0f, dir,
				(int)FX_flrand(2, 4), 0);
	}
}

static void
TE_Streaks(const vec3_t org, int count, int sprite, float lmin, float lmax, float qr,
		qboolean zpos, float arate_fixed)
{
	vec3_t q, e;
	fx_part_t *l;
	float L;
	int i;

	for (i = 0; i < count; i++)
	{
		L = (lmin == lmax) ? lmin : (float)FX_irand((int)lmin, (int)lmax);

		if (zpos)
		{
			VectorSet(q, FX_flrand(-qr, qr), FX_flrand(-qr, qr), FX_flrand(0, qr));
		}
		else
		{
			FX_RandVec(q, qr);
		}

		VectorAdd(org, q, e);
		l = FX_Line(org, e, sprite, 2, 250, 250, 250, 250, L, 1, false); /* white 250 (I5.15) */

		if (l)
		{
			/* start velocity 20q, end velocity 40q, no acceleration: the streak stretches */
			VectorScale(q, 20, l->vel);
			VectorScale(q, 40, l->endvel);

			if (arate_fixed)
			{
				l->arate = arate_fixed;
			}
			else
			{
				FX_FadeOverLife(l, L);
			}
		}
	}
}

static void
FX_FireballsAndTimers(const vec3_t org, float size, float spread)
{
	fx_puff_t pf;
	int i;

	for (i = 0; i < 8; i++)
	{
		FX_PuffInit(&pf);
		pf.sprite = 28;
		pf.blend = 1;
		pf.life_ms = 500;
		pf.w = pf.h = size;
		pf.r = pf.g = pf.b = 250;
		pf.a = 150;
		pf.rot = C628;
		FX_RandVec(pf.vel, spread);
		pf.avel = C628;
		pf.grow = 40;
		pf.arate = -2.0f * pf.a;
		FX_Puff(org, &pf);
	}

	FX_Timer(org, FX_CB_FireballA, 70, 50, 1500);
	FX_Timer(org, FX_CB_FireballA, 70, 50, 1500);
	FX_Timer(org, FX_CB_FireballB, 40, 50, 1500);
	FX_Timer(org, FX_CB_SmokeC, 30, 0, 1500);
}

static void
TE_WhiteSmokeExplosion(void)
{
	vec3_t org, v, q, e, start, end;
	fx_puff_t pf;
	fx_part_t *p, *l;
	trace_t tr;
	int i, n, hit;
	float L;

	FX_ReadPos(org);
	/* Ghoul explosion model (whitesmoke_tile): created but never drawn in 1.07f */

	n = FX_irand(3, 4);

	for (i = 0; i < n; i++)
	{
		VectorSet(v, FX_flrand(-200, 200), FX_flrand(-200, 200), FX_flrand(0, 300));
		FX_BurningBit(org, 12, (float)FX_irand(1000, 1200), v, -300, FX_flrand(-10, -8),
				FX_flrand(-255, -175), FX_TrailColouredSmoke, 0.075f);
	}

	n = FX_irand(4, 5);

	for (i = 0; i < n; i++)
	{
		VectorSet(v, FX_flrand(-200, 200), FX_flrand(-200, 200), FX_flrand(0, 400));
		FX_BurningBit(org, 8, (float)FX_irand(1500, 2000), v, -300, -4, -500, FX_TrailFire, 0.05f);
	}

	for (i = 0; i < 3; i++)
	{
		FX_PuffInit(&pf);
		FX_Orange(&pf);
		pf.sprite = 48;
		pf.blend = 1;
		L = (float)FX_irand(500, 1000);
		pf.life_ms = L;
		pf.w = pf.h = 6;
		VectorSet(pf.vel, FX_flrand(-100, 100), FX_flrand(-100, 100), FX_flrand(0, 60));
		pf.rot = FX_RandRot(); /* I5.15 */
		pf.grow = 6.0f / (L / 1000.0f);
		p = FX_Puff(org, &pf);
		FX_FadeOverLife(p, L);
	}

	n = FX_irand(5, 6);
	VectorCopy(org, e);

	for (i = 0; i < n; i++)
	{
		L = (float)FX_irand(400, 500);
		VectorSet(q, FX_flrand(-6, 6), FX_flrand(-6, 6), FX_flrand(0, 6));
		VectorAdd(org, q, e);
		l = FX_Line(org, e, 55, 2, 250, 250, 250, 250, L, 1, false); /* white 250 (I5.15) */

		if (l)
		{
			/* start velocity 20q, end velocity 40q, no acceleration */
			VectorScale(q, 20, l->vel);
			VectorScale(q, 40, l->endvel);
			FX_FadeOverLife(l, L);
		}
	}

	FX_Timer(e, FX_CB_SmokeBallA, 20, 0, 500);
	FX_Timer(e, FX_CB_SmokeBallB, 20, 0, 500);
	FX_Timer(e, FX_CB_SmokeBallA, 20, 0, 500);

	for (i = 0; i < 4; i++)
	{
		VectorCopy(e, start);
		start[2] += 64;
		VectorSet(end, e[0] + FX_flrand(-128, 128), e[1] + FX_flrand(-128, 128), e[2] - 64);
		FX_Trace(start, NULL, NULL, end, FXTR_BRUSHES, &tr, &hit);

		if (tr.fraction < 1.0f)
		{
			VectorScale(start, 0.1f, v);
			VectorMA(v, 0.9f, tr.endpos, v);
			FX_Timer(v, FX_CB_GroundSmoke, 50, 0, 2000);
		}
	}
}

static void
TE_C4(void)
{
	vec3_t org;
	fx_part_t *f;
	int i;

	FX_ReadPos(org);
	(void)MSG_ReadByte(&net_message);

	for (i = 0; i < 2; i++)
	{
		f = FX_Flash(org, 360, 250, 250, 250, 250); /* "white" = 250 (I5.15) */

		if (f)
		{
			f->avel = FX_flrand(-1, 1);
			f->r1 = f->r2 = -640;

			if (i == 1)
			{
				f->q.flags |= AQ_ORIENTED;
				VectorSet(f->q.normal, 0, 0, 1);
			}
		}
	}

	FX_FireballsAndTimers(org, 20, 100);
	FX_PlaySound(58, org, 0.8f, 2.0f, -1, 0, 0);
}

static void
TE_BloodStream(void)
{
	vec3_t org, step, o;
	fx_puff_t pf;
	int i, a = 255;

	FX_ReadPos(org);
	FX_ReadPos(step);

	for (i = 0; i < 30; i++)
	{
		VectorMA(org, 3.0f * i, step, o);
		FX_PuffInit(&pf);
		pf.sprite = 1;
		pf.life_ms = 2500;
		pf.h = 0.5f + 0.08f * i; /* I5.3: +0x0c (along up) */
		pf.w = 1.5f + 0.08f * i;
		pf.r = 63;
		pf.g = 15;
		pf.b = 15;
		pf.a = a;
		pf.rot = FX_flrand(-0.175f, 0.175f);
		VectorSet(pf.vel, 0, 0, -10);
		pf.grow = 1;
		pf.arate = -1.4286f * a;
		FX_Puff(o, &pf);
		a = (int)(0.95f * a);
	}
}

static void
TE_BigExplosion(void)
{
	vec3_t org, v;
	fx_puff_t pf;
	fx_part_t *p;
	int m, i, k;
	float L;

	FX_ReadPos(org);
	m = MSG_ReadByte(&net_message);
	k = (m >> 6) + 2;
	/* Ghoul explosion models (explosion_tile): created but never drawn in 1.07f */

	for (i = 0; i < (m >> 5); i++)
	{
		VectorSet(v, FX_flrand(-300, 300), FX_flrand(-300, 300), FX_flrand(0, 450));
		FX_BurningBit(org, 24, (float)FX_irand(1000, 1200), v, -450, -20, FX_flrand(-255, -175),
				FX_TrailFireSmoke, 0.075f);
	}

	for (i = 0; i < k; i++)
	{
		VectorSet(v, FX_flrand(-300, 300), FX_flrand(-300, 300), FX_flrand(0, 600));
		FX_BurningBit(org, 12, (float)FX_irand(1500, 2000), v, -450, -6, -255, FX_TrailFire, 0.05f);
	}

	for (i = 0; i < k; i++)
	{
		FX_PuffInit(&pf);
		FX_Orange(&pf);
		pf.sprite = 48;
		pf.blend = 1;
		L = (float)FX_irand(400, 600);
		pf.life_ms = L;
		pf.w = pf.h = 6;
		FX_RandVec(pf.vel, 100);
		pf.rot = FX_RandRot(); /* I5.15 */
		pf.avel = FX_flrand(-3, 3);
		pf.grow = 30.0f / (L / 1000.0f);
		p = FX_Puff(org, &pf);
		FX_FadeOverLife(p, L);
	}

	TE_Streaks(org, (m >> 5) + 4, 54, 600, 600, 8, false, -416.7f);

	p = FX_Flash(org, 360, 250, 250, 250, 100);

	if (p)
	{
		p->avel = FX_flrand(-1, 1);
		p->r1 = p->r2 = -640;
		p->arate = -200;
	}

	FX_FireballsAndTimers(org, 20, 100);
	FX_PlaySound(58, org, 1.0f, 0.0f, -1, 0, 0);
	FX_TraceLog("TE 44 explosion magnitude %d\n", m);
}

static void
TE_ColouredSmoke(void)
{
	vec3_t org;
	fx_puff_t pf;

	FX_ReadPos(org);
	FX_PuffInit(&pf);
	pf.r = MSG_ReadByte(&net_message);
	pf.g = MSG_ReadByte(&net_message);
	pf.b = MSG_ReadByte(&net_message);
	pf.a = MSG_ReadByte(&net_message);
	pf.sprite = 0;
	pf.lit = true;
	pf.life_ms = 1200;
	pf.w = pf.h = 10;
	pf.rot = C628;
	pf.avel = C628;
	VectorSet(pf.vel, 0, 0, 30);
	VectorSet(pf.accel, 0, 0, 100);
	pf.grow = 0.6f;
	pf.arate = -1.6667f * pf.a;
	FX_Puff(org, &pf);
}

static void
TE_SmallExplosion(int type)
{
	vec3_t org;
	fx_puff_t pf;
	fx_part_t *p;
	float s;
	int size, sound, i;

	FX_ReadPos(org);
	size = MSG_ReadByte(&net_message);
	sound = MSG_ReadByte(&net_message);
	s = size * 0.01f;

	if (!((type == 56) && (sound == 2)))
	{
		p = FX_Flash(org, 30 * s, 250, 250, 250, 100);

		if (p)
		{
			FX_SetLife(p, 0.3f);
			p->avel = FX_flrand(-1, 1);
			p->r1 = p->r2 = -33.33f * s;
			p->arate = -333.3f;
		}

		for (i = 0; i < 8; i++)
		{
			FX_PuffInit(&pf);
			pf.sprite = 28;
			pf.blend = 1;
			pf.life_ms = 500;
			pf.w = pf.h = 20 * s;
			pf.r = pf.g = pf.b = 250;
			pf.a = 150;
			FX_RandVec(pf.vel, 50 * s);
			pf.rot = FX_RandRot(); /* I5.15 */
			pf.avel = FX_flrand(-1, 1);
			pf.grow = -33.33f * s;
			pf.arate = -2.0f * pf.a;
			FX_Puff(org, &pf);
		}
	}

	if (sound)
	{
		FX_PlaySound((type == 46) ? 91 : ((type == 55) ? 90 : 89), org, 1.0f, 0.0f, -1, 0, 0);
	}

	FX_TraceLog("TE %d explosion size %d sound %d\n", type, size, sound);
}

static void
TE_ShockRing(void)
{
	vec3_t org;
	fx_puff_t pf;
	int S;

	FX_ReadPos(org);
	S = (int)(MSG_ReadShort(&net_message) * 1.5f);
	FX_PuffInit(&pf);
	pf.sprite = 22;
	pf.blend = 1;
	pf.life_ms = 300;
	pf.w = pf.h = (float)S;
	pf.r = pf.g = pf.b = pf.a = 250; /* "white" = 250, rotation starts at 0 (I5.15) */
	pf.flat = true;
	pf.avel = 7;
	pf.grow = -0.6667f * S;
	pf.arate = -833.3f;
	FX_Puff(org, &pf);
}

static void
TE_DustPuffs52(void)
{
	vec3_t org;
	fx_puff_t pf;
	int i, count;

	FX_ReadPos(org);
	count = MSG_ReadShort(&net_message);

	for (i = 0; i < count; i++)
	{
		FX_PuffInit(&pf);
		pf.sprite = 9;
		pf.lit = true;
		pf.life_ms = 1000;
		pf.w = pf.h = 5.0f + 2 * i;
		pf.r = pf.g = pf.b = 250;
		pf.a = (100 - 10 * i) & 0xFF;
		pf.rot = FX_flrand(-(float)M_PI, (float)M_PI);
		VectorSet(pf.accel, 0, 0, -100.0f - 40 * i);
		pf.grow = 5.0f + 2 * i;
		pf.arate = -(100.0f + 10 * i);
		FX_Puff(org, &pf);
	}
}

static void
FX_MuzzlePoint(int shooter, const vec3_t start, vec3_t out)
{
	if (FX_IsLocalPlayer(shooter))
	{
		VectorCopy(cl.refdef.vieworg, out);
		return;
	}

	/* in the frame: bolt `flash` of the model at `wbolt_hand_r` (fx_ghoul.c); otherwise (not in the
	   frame, no such model, no Ghoul drawing) the payload start */
	if (FXG_MuzzlePoint(shooter, out))
	{
		FX_TraceLog("  muzzle: bolt flash at (%.1f %.1f %.1f), payload start (%.1f %.1f %.1f)\n", out[0],
				out[1], out[2], start[0], start[1], start[2]);
		return;
	}

	VectorCopy(start, out);
}

static void
FX_ShooterEffect(int index, int shooter, const vec3_t muzzle, const vec3_t dir)
{
	fx_params_t p;

	if (FX_IsLocalPlayer(shooter) || !index)
	{
		return;
	}

	FX_DefaultParams(&p);
	VectorCopy(muzzle, p.origin);
	VectorCopy(dir, p.fwd);
	VectorSet(p.right, 0, 1, 0);
	VectorSet(p.up, 0, 0, 1);
	FX_StartEffect(FX_ConfigEffect(index), &p, FXS_GAME);
}

static void
TE_MPBullet(int type)
{
	vec3_t start, end, dir, muzzle;
	int index, shooter;

	if (type == 63)
	{
		index = MSG_ReadByte(&net_message);
		shooter = MSG_ReadByte(&net_message);
		FX_ReadPos(start);
		FX_ReadScaledPos(dir, 1.0f / 2048.0f);
		FX_MuzzlePoint(shooter, start, muzzle);
		FX_TraceLog("TE 63 projectile fired by %d%s effect %d\n", shooter,
				FX_IsLocalPlayer(shooter) ? " (local)" : "", index);
		FX_ShooterEffect(index, shooter, muzzle, dir);
		return;
	}

	index = MSG_ReadByte(&net_message);
	(void)MSG_ReadByte(&net_message);
	shooter = MSG_ReadByte(&net_message);
	FX_ReadPos(start);

	if (type == 60)
	{
		FX_ReadPos(end);
		FX_MuzzlePoint(shooter, start, muzzle);
		VectorSubtract(end, muzzle, dir);
		VectorNormalize(dir);
		FX_TraceLog("TE 60 bullet by %d%s effect %d\n", shooter,
				FX_IsLocalPlayer(shooter) ? " (local)" : "", index);
		FX_ShooterEffect(index, shooter, muzzle, dir);
		return;
	}

	FX_ReadScaledPos(dir, 1.0f / 2048.0f);
	FX_MuzzlePoint(shooter, start, muzzle);
	FX_TraceLog("TE %d bullet by %d%s effect %d\n", type, shooter,
			FX_IsLocalPlayer(shooter) ? " (local)" : "", index);
	FX_PelletImpact(muzzle, dir, 1024, -2, 4.0f, dir, (type == 61) ? 4 : 7, 0.613f);
	FX_ShooterEffect(index, shooter, muzzle, dir);
}

/* type 35: blood from a bolt of a server Ghoul instance; 41 (same payload) is broken in 1.07f and
   draws nothing */
static void
TE_BloodDrip(int type)
{
	vec3_t org, fwd, right, up, end;
	fx_impact_t im;
	trace_t tr;
	int handle, bolt, ent, hit;

	handle = MSG_ReadShort(&net_message);
	bolt = MSG_ReadShort(&net_message);
	ent = MSG_ReadShort(&net_message);
	(void)MSG_ReadByte(&net_message); /* delay of an invisible timer without effect */

	if ((type == 41) || !FXG_BoltFrame(ent, handle, bolt, false, org, fwd, right, up, false))
	{
		return;
	}

	FX_TraceLog("  bolt %d of instance %d at (%.0f %.0f %.0f)\n", bolt, handle, org[0], org[1], org[2]);
	VectorCopy(org, end);
	end[2] -= 320;
	FX_Trace(org, NULL, NULL, end, FXTR_BRUSHES, &tr, &hit);

	if (tr.fraction < 1.0f)
	{
		memset(&im, 0, sizeof(im));
		im.kind = hit ? 2 : 0;
		im.ent = hit ? hit : -1;
		VectorCopy(tr.endpos, im.point);
		VectorCopy(tr.plane.normal, im.normal);
		im.material = FX_TraceMaterial(&tr);
		im.surfflags = tr.surface ? (tr.surface->flags & 0xFFFFFF) : 0;
		im.leaf = CM_PointLeafnum(im.point);
		FX_AddDecal(&im, FX_SpriteName(1), 100, 0, 0, 250, FX_flrand(10, 15), -1, 0, 0, 0);
	}

	FX_FleshDrops(org, fwd, 60, 0, 0, 200, 3);
}

/* type 37: a limb-smoke timer per limb-cap / body bolt present and switched on, at the world origin
   instead of the bolt (1.07f quirk) */
static void
TE_LimbSmoke(void)
{
	vec3_t zero = {0, 0, 0};
	int handle = MSG_ReadShort(&net_message), i, n = 0;

	(void)MSG_ReadShort(&net_message);

	for (i = 0; i < (int)(sizeof(fx_limb_bolts) / sizeof(fx_limb_bolts[0])); i++)
	{
		if (FXG_InstanceHasPart(handle, fx_limb_bolts[i]))
		{
			FX_Timer(zero, FX_CB_LimbSmoke, 100, 0.6f, 4000);
			n++;
		}
	}

	FX_TraceLog("  limb smoke timers x%d (instance %d)\n", n, handle);
}

static void
TE_DebugBox(void)
{
	vec3_t mins, maxs, c[8];
	int r, g, b, i;
	static const int edges[12][2] = {
		{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6},
		{3, 7}
	};

	FX_ReadPos(mins);
	FX_ReadPos(maxs);
	r = MSG_ReadByte(&net_message);
	g = MSG_ReadByte(&net_message);
	b = MSG_ReadByte(&net_message);

	for (i = 0; i < 8; i++)
	{
		c[i][0] = (i & 1) ? maxs[0] : mins[0];
		c[i][1] = (i & 2) ? maxs[1] : mins[1];
		c[i][2] = (i & 4) ? maxs[2] : mins[2];
	}

	for (i = 0; i < 12; i++)
	{
		FX_Line(c[edges[i][0]], c[edges[i][1]], 14, 1, r, g, b, 165, 400, 0, false);
	}
}

/* ---- dispatch ------------------------------------------------------------------------- */

void
CLFX_ParseTempEntity(int type)
{
	vec3_t org, dir, v;
	fx_puff_t pf;
	fx_part_t *p;
	int i, b;
	int serial0 = FX_ParticleSerial(), lights0 = cl_sof_lights_added;

	CLFX_Init();

	if ((type < 0) || (type > 64))
	{
		Com_Error(ERR_DROP, "SoF: temp entity type %d", type);
		return;
	}

	FX_TraceLog("svc_temp_entity %d\n", type);
	FX_ShotEvent('t', type);

	switch (type)
	{
		case 0:
		case 14:
		case 10:
			FX_ReadPos(org);
			FX_ReadDir(dir);
			FX_SparkBurst(org, dir, type == 10);
			break;

		case 1:
		case 51:
			FX_ReadPos(org);
			break;

		case 2:
			TE_Spray();
			break;

		case 3:
		case 29:
		case 30:
			TE_ImpactDecal(type);
			break;

		case 4:
		case 15:
		case 17:
		case 28:
		case 53:
			TE_FloorDecal(type);
			break;

		case 5:
		case 22:
			break;

		case 6:
		case 7:
			TE_WeaponImpact(type);
			break;

		case 8:
			FX_ReadPos(org);
			b = MSG_ReadByte(&net_message);
			FX_ReadDir(dir);
			VectorMA(org, (float)b, dir, v);
			FX_ElectricArc(org, v, (float)(FX_crand() % 3 + 3), false);
			break;

		case 9:
			b = MSG_ReadByte(&net_message);
			FX_ReadScaledPos(dir, 0.001f);
			FX_ReadPos(v);
			(void)MSG_ReadByte(&net_message);
			(void)MSG_ReadByte(&net_message);
			VectorMA(cl.refdef.vieworg, (float)b, dir, org);
			FX_PlaySound(48 + FX_crand() % 4, org, 1.0f, 1.0f, 0, 0, 0);
			break;

		case 11:
			FX_ReadPos(org);
			i = MSG_ReadShort(&net_message);
			b = MSG_ReadByte(&net_message);
			FX_PuffInit(&pf);
			pf.sprite = 17;
			pf.lit = true;
			pf.life_ms = 2000;
			pf.h = 1.5f * b; /* SPEC-AMEND-005 I5.3: "a x b" = +0x0c x +0x10 */
			pf.w = (float)b;
			pf.r = ((i >> 8) & 0xF) * 15;
			pf.g = ((i >> 4) & 0xF) * 15;
			pf.b = (i & 0xF) * 15;
			pf.a = 250;
			pf.rot = (float)M_PI;
			VectorSet(pf.accel, 0, 0, -800);
			FX_Puff(org, &pf);
			break;

		case 12:
			TE_ElectricBurst();
			break;

		case 13:
			TE_GroundFlash();
			break;

		case 16:
			TE_Lightning();
			break;

		case 18:
			TE_Debris();
			break;

		case 19:
			TE_ExplosionDebris();
			break;

		case 20:
			TE_FlashGrenade();
			break;

		case 21:
			TE_ShockPuff();
			break;

		case 23:
			TE_Thud();
			break;

		case 24:
			TE_ShotgunPellets();
			break;

		case 25:
			TE_Bubbles();
			break;

		case 26:
			TE_SmokeBurst();
			break;

		case 27:
			TE_DebugLine();
			break;

		case 31:
			TE_LitSparks();
			break;

		case 32:
			TE_KnifeImpact();
			break;

		case 33:
			TE_ScatterExplosion();
			break;

		case 34:
			TE_ShotgunBlast();
			break;

		case 35:
		case 41:
			TE_BloodDrip(type);
			break;

		case 36:
			for (i = 0; i < 5; i++)
			{
				(void)MSG_ReadShort(&net_message);
			}

			break;

		case 37:
			TE_LimbSmoke();
			break;

		case 38:
			TE_WhiteSmokeExplosion();
			break;

		case 39:
			(void)MSG_ReadShort(&net_message);
			VectorClear(org);
			FX_Timer(org, FX_CB_OriginSmoke, 15, 0, 1500);
			break;

		case 40:
			TE_C4();
			break;

		case 42:
			for (i = 0; i < 3; i++)
			{
				(void)MSG_ReadShort(&net_message);
			}

			break;

		case 43:
			TE_BloodStream();
			break;

		case 44:
			TE_BigExplosion();
			break;

		case 45:
			TE_ColouredSmoke();
			break;

		case 46:
		case 55:
		case 56:
			TE_SmallExplosion(type);
			break;

		case 47:
			TE_ShockRing();
			break;

		case 48:
			FX_ReadPos(org);
			i = MSG_ReadShort(&net_message);
			FX_BlowingPaper(org, i);
			break;

		case 49:
			FX_ReadPos(org);
			FX_ReadDir(dir);
			FX_SparkShower(org, dir);
			break;

		case 50:
			FX_ReadPos(org);
			FX_ReadPos(dir);
			b = MSG_ReadShort(&net_message) & 0xFF;
			i = MSG_ReadShort(&net_message) & 0xFF;
			FX_SodaCans(org, dir, b, i ? i : FX_irand(1, 4));
			break;

		case 52:
			TE_DustPuffs52();
			break;

		case 54:
			FX_ReadPos(org);
			FX_PlaySound(34 + FX_crand() % 3, org, 0.6f, 1.0f, 0, 0, 0);
			break;

		case 57:
			FX_ReadPos(org);
			FX_ReadPos(v);
			p = FX_Line(org, v, 14, 1, 250, 250, 250, 125, 5, 1, false);

			if (p)
			{
				p->arate = -250;
			}

			break;

		case 58:
		case 59:
			FX_ReadPos(org);
			FX_PlaySound(type == 58 ? 92 : 93, org, 1.0f, 1.0f, 0, 0, 0);
			break;

		case 60:
		case 61:
		case 62:
		case 63:
			TE_MPBullet(type);
			break;

		case 64:
			TE_DebugBox();
			break;
	}

	FX_TraceCreated(serial0, lights0);
}

/* shared paper limit (impacts and type 48) */
qboolean
FX_LimitPaper(void)
{
	if (cl.time < fx_lim_paper - 3000)
	{
		fx_lim_paper = -100000;
	}

	if (cl.time - fx_lim_paper < 300)
	{
		return false;
	}

	fx_lim_paper = cl.time;
	return true;
}
