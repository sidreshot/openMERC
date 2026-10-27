/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Client effects: impacts on the 43 surface materials (mark and impact sound,
 * material effects, dust puffs, impact chunks, glass shards, the spark
 * emitter), footsteps and the other entity events.
 *
 * Implements: SPEC-401 (Impact handling, Material impact effects, Dust puffs,
 * Impact chunks, Spark emitter, Entity events, Footsteps), SPEC-400 (Debris
 * item 5: chunk settling), SPEC-201 s.1 (impact descriptor)
 */

#include "fx_local.h"

#define C628 ((float)(FX_crand() % 628) / 100.0f)

static int fx_lim_pipe, fx_lim_vent, fx_lim_computer, fx_lim_glass, fx_lim_vending;
static int fx_lim_canburst;
int fx_lim_paper;
static int fx_chunk_time[FX_NUM_MATERIALS];

void
FX_ClearTempState(void)
{
	int i;

	fx_lim_pipe = fx_lim_vent = fx_lim_computer = fx_lim_glass = fx_lim_vending = -100000;
	fx_lim_canburst = fx_lim_paper = -100000;

	for (i = 0; i < FX_NUM_MATERIALS; i++)
	{
		fx_chunk_time[i] = -100000; /* no burst yet (a fresh client time can be below 200) */
	}
}

/* rate limits run on client time; a backwards jump of 3 s or more resets them */
static qboolean
FX_Limit(int *last, int interval)
{
	if (cl.time < *last - 3000)
	{
		*last = -100000;
	}

	if (cl.time - *last < interval)
	{
		return false;
	}

	*last = cl.time;
	return true;
}

/* ---- impact descriptor --------------------------------------------------------- */

static void
FX_RotateYaw(const vec3_t in, float yaw, vec3_t out)
{
	float s = sinf(FX_DEG2RAD(yaw)), c = cosf(FX_DEG2RAD(yaw));
	vec3_t t;

	t[0] = in[0] * c - in[1] * s;
	t[1] = in[0] * s + in[1] * c;
	t[2] = in[2];
	VectorCopy(t, out);
}

qboolean
FX_ReadImpact(fx_impact_t *im)
{
	vec3_t start, end, off, angles, org;
	trace_t tr;
	int hit;

	memset(im, 0, sizeof(*im));
	im->ent = -1;
	im->material = -1;
	im->hole = -1;
	im->kind = MSG_ReadByte(&net_message);

	switch (im->kind)
	{
		case 0:
			FX_ReadPos(im->point);
			FX_ReadScaledPos(im->normal, 0.001f);

			/* re-trace to find the surface: the client's movement trace, i.e. the world and
			   brush-model packet entities only (SPEC-AMEND-005 I5.17). Kind 3 when a brush
			   entity is hit first, and also when nothing is hit (end point, zero normal, the
			   empty surface: material 0, no flags) */
			VectorMA(im->point, 5, im->normal, start);
			VectorMA(im->point, -5, im->normal, end);
			FX_Trace(start, NULL, NULL, end, FXTR_BRUSHES, &tr, &hit);

			if (tr.fraction >= 1.0f)
			{
				im->kind = 3;
				VectorCopy(tr.endpos, im->point);
				VectorClear(im->normal);
				im->material = 0;
				im->surfflags = 0;
				break;
			}

			if (hit > 0)
			{
				im->kind = 3;
				im->ent = hit;
			}

			im->material = FX_TraceMaterial(&tr);
			im->surfflags = tr.surface ? (tr.surface->flags & 0xFFFFFF) : 0;
			break;

		case 1:
			FX_ReadPos(im->point);
			VectorSet(im->normal, 0, 0, 1);
			break;

		case 2:
			FX_ReadPos(off);
			FX_ReadScaledPos(im->normal, 0.001f);
			im->ent = MSG_ReadShort(&net_message);
			im->material = MSG_ReadByte(&net_message);
			FX_EntityLerpOrigin(im->ent, org);
			FX_EntityLerpAngles(im->ent, angles);
			FX_RotateYaw(off, angles[YAW], off);
			FX_RotateYaw(im->normal, angles[YAW], im->normal);
			VectorAdd(org, off, im->point);
			break;

		case 3:
			FX_ReadPos(im->point);
			FX_ReadScaledPos(im->normal, 0.001f); /* raw horizontal offset used as "normal" */
			im->material = MSG_ReadByte(&net_message);
			break;

		default:
			Com_Error(ERR_DROP, "SoF: impact kind %d", im->kind);
			return false;
	}

	im->leaf = CM_PointLeafnum(im->point);
	return true;
}

/* ---- mark and impact sound -------------------------------------------------------- */

static void
FX_ImpactSound(const fx_impact_t *im, float vol)
{
	int s = fx_materials[im->material].impact;

	if (s == 44)
	{
		s = FX_irand(79, 81);
	}

	FX_PlaySound(s, im->point, vol, 1.0f, 0, 0, 0);
}

float
FX_ImpactVolume(float power)
{
	float v = (power == 0) ? 0.6f : 0.4f + power * 0.053333f;

	return v > 1 ? 1 : v;
}

float
FX_FootVolume(float volume, float dist)
{
	return volume * ((dist > 50) ? 50.0f / dist : 1.0f);
}

void
FX_MarkAndSound(fx_impact_t *im, float volume_override)
{
	const fx_material_t *mat;
	float v;

	if ((im->material < 0) || (im->material >= FX_NUM_MATERIALS))
	{
		return;
	}

	if (im->surfflags & (SURF_SKY | SURF_NODRAW))
	{
		return;
	}

	mat = &fx_materials[im->material];

	if (im->markmode == 4)
	{
		v = FX_ImpactVolume(im->power);

		if (volume_override > 0)
		{
			v = volume_override;
		}

		if (((im->power > 1.5f) || (im->power == 0)) && (im->material != 35))
		{
			FX_ImpactSound(im, v);
		}

		if (im->power > 3)
		{
			im->power = 3;
		}

		if (im->hole >= 0)
		{
			FX_AddDecal(im, FX_SpriteName(im->hole), mat->rgba[0], mat->rgba[1], mat->rgba[2],
					(float)FX_irand(200, 240), im->power * 0.6f + FX_flrand(0, 0.1f), -1, 0, 0, 0);
		}
	}
	else if (im->markmode == 5)
	{
		FX_AddDecal(im, FX_SpriteName(26), 230, 230, 230, 230, FX_flrand(32, 36), -1, 0, 0, 0);
	}
	else if (mat->markmode == 2)
	{
		FX_AddDecal(im, FX_SpriteName(im->hole), mat->rgba[0], mat->rgba[1], mat->rgba[2],
				im->alphafactor * 255.0f, 8, -1, 0, 0, -(float)M_PI / 2.0f - im->yaw);
	}
}

/* ---- material effects ----------------------------------------------------------------- */

static void
FX_ImpactAxes(const fx_impact_t *im, fx_params_t *p)
{
	FX_DefaultParams(p);
	VectorCopy(im->point, p->origin);
	VectorCopy(im->normal, p->fwd);
	FX_Perpendicular(im->normal, p->right);
	CrossProduct(im->normal, p->right, p->up);
}

static void
FX_ImpactNamed(const fx_impact_t *im, const char *name)
{
	fx_params_t p;

	FX_ImpactAxes(im, &p);
	FX_StartNamed(name, &p, FXS_GAME);
}

/* the impact surface would accept a decal (SPEC-400 Decals item 1) */
static qboolean
FX_DecalSurface(const fx_impact_t *im)
{
	if ((im->kind == 1) || (im->kind == 3))
	{
		return false;
	}

	if ((im->kind == 0) && (im->surfflags & (SURF_SKY | SURF_WARP | SURF_FLOWING)))
	{
		return false;
	}

	if ((im->kind == 2) && (im->material != 37))
	{
		return false;
	}

	return true;
}

/* dust puffs, styles 0..2 (SPEC-401 Dust puffs); count < 0 = from power */
static void
FX_DustPuffs(const fx_impact_t *im, const vec3_t d, int p, int style, int count)
{
	static const struct
	{
		float A, B, C, L, G;
		int sprite;
	} st[3] = {
		{30, 10, -150, 600, 3, 9},
		{30, 10, -120, 600, 2, 12},
		{20, 10, -100, 500, 2, 12}
	};
	fx_puff_t pf;
	const fx_material_t *mat = &fx_materials[im->material];
	float f1, f2;
	int N, i;

	N = (count >= 0) ? count : ((p <= 4) ? p : (int)((p - 4) / 2.0f + 4));
	f1 = FX_flrand(0.8f, 1.2f);
	f2 = FX_flrand(0.8f, 1.2f);

	for (i = 1; i <= N; i++)
	{
		FX_PuffInit(&pf);
		pf.sprite = st[style].sprite;
		pf.lit = true;
		pf.lightdir = FX_DirIndex(im->normal);
		pf.w = pf.h = (float)N;
		pf.r = mat->rgba[0];
		pf.g = mat->rgba[1];
		pf.b = mat->rgba[2];
		pf.a = mat->rgba[3];
		pf.life_ms = st[style].L;
		pf.rot = FX_flrand(0, 2.0f * (float)M_PI);
		VectorScale(d, st[style].B * i * f2, pf.vel);
		VectorMA(pf.vel, st[style].A * i * f1, im->normal, pf.vel);
		pf.vel[2] += 8.0f * i;
		VectorSet(pf.accel, 0, 0, st[style].C * i);
		pf.grow = st[style].G * i / (st[style].L / 1000.0f);
		pf.arate = -(float)pf.a / (pf.life_ms / 1000.0f);
		FX_Puff(im->point, &pf);
	}

	FX_TraceLog("  dust puffs style %d x%d\n", style, N);
}

static void
FX_ConcretePuffs(const fx_impact_t *im, int p)
{
	fx_puff_t pf;
	int i, k;

	for (i = 0; i < 3; i++)
	{
		FX_PuffInit(&pf);
		pf.sprite = 0;
		pf.lit = true;
		pf.life_ms = 600;
		pf.w = pf.h = p * 1.5f;
		pf.r = pf.g = pf.b = 200;
		pf.a = 100;

		for (k = 0; k < 3; k++)
		{
			pf.vel[k] = (im->normal[k] + FX_flrand(-0.1f, 0.1f)) * (FX_crand() % 32 + 16) +
				FX_crand() % 16 - 7;
		}

		VectorSet(pf.accel, 0, 0, 40);
		pf.rot = C628;
		pf.avel = C628;
		pf.grow = p * 1.6667f;
		pf.arate = -(float)pf.a / 0.6f;
		FX_Puff(im->point, &pf);
	}

	FX_TraceLog("  concrete puffs x3 size %.1f\n", p * 1.5f);
}

void
FX_FleshDrops(const vec3_t org, const vec3_t n, int r, int g, int b, int a, float size)
{
	fx_puff_t pf;
	int i;

	for (i = 0; i < 5; i++)
	{
		FX_PuffInit(&pf);
		pf.sprite = 0;
		pf.lit = true;
		pf.w = pf.h = size;
		pf.r = r;
		pf.g = g;
		pf.b = b;
		pf.a = a;
		pf.life_ms = 600;
		VectorSet(pf.vel, 15.0f * i * n[0], 15.0f * i * n[1], 10.0f * i);
		VectorSet(pf.accel, 0, 0, -40.0f * i);
		pf.rot = C628;
		pf.avel = C628;
		pf.grow = 1.6667f * i;
		pf.arate = -(float)a / 0.6f;
		FX_Puff(org, &pf);
	}
}

void
FX_MaterialEffect(const fx_impact_t *im, const vec3_t dir, int power)
{
	const fx_material_t *mat;
	qboolean shot;
	fx_params_t p;
	vec3_t org, d;
	char name[MAX_QPATH];
	int n;

	if ((im->material < 0) || (im->material >= FX_NUM_MATERIALS))
	{
		return;
	}

	mat = &fx_materials[im->material];
	shot = (im->markmode == 4) || (im->markmode == 5);
	VectorCopy(dir, d);

	switch (mat->effect)
	{
		case FXM_DUST:
			FX_DustPuffs(im, d, power, 0, -1);
			break;

		case FXM_CONCRETE:
			if (shot)
			{
				FX_ConcretePuffs(im, power);
				FX_DustPuffs(im, d, power, 2, (power <= 2) ? 1 : 2);
			}

			break;

		case FXM_METAL:
			if (shot)
			{
				FX_ImpactNamed(im, "environ/wallspark");
			}

			break;

		case FXM_WATER:
			FX_DefaultParams(&p);
			VectorCopy(im->point, p.origin);
			VectorSet(p.fwd, 0, 0, 1);
			VectorSet(p.right, 1, 0, 0);
			VectorSet(p.up, 0, 1, 0);
			FX_StartNamed("environ/bulletsplash", &p, FXS_GAME);
			break;

		case FXM_SPLAT:
			if (shot)
			{
				Com_sprintf(name, sizeof(name), "environ/splat%s", mat->arg);
				FX_ImpactNamed(im, name);
			}

			break;

		case FXM_PIPE:
		case FXM_VENT:
			if (!shot)
			{
				break;
			}

			FX_ImpactNamed(im, "environ/wallspark");

			if (!FX_DecalSurface(im) ||
				!FX_Limit(mat->effect == FXM_PIPE ? &fx_lim_pipe : &fx_lim_vent, 300))
			{
				break;
			}

			FX_DefaultParams(&p);
			VectorCopy(im->point, p.origin);
			VectorCopy(im->normal, p.fwd);

			if (fabs(im->normal[2]) > 0.9f)
			{
				FX_Perpendicular(im->normal, p.right);
			}
			else
			{
				VectorSet(p.right, 0, 0, 1);
			}

			CrossProduct(p.fwd, p.right, p.up);
			FX_StartNamed(mat->effect == FXM_PIPE ? mat->arg : "environ/metal_steam", &p, FXS_GAME);
			break;

		case FXM_COMPUTER:
			if (!shot || !FX_Limit(&fx_lim_computer, 300))
			{
				break;
			}

			FX_ImpactNamed(im, "environ/metal_computer");
			VectorMA(im->point, 5, im->normal, org);
			FX_SparkEmitter(org, im->normal, (float)FX_irand(4000, 8000));
			FX_ImpactChunks(im, 18, 6, 50, -1);
			break;

		case FXM_FLESH:
			FX_FleshDrops(im->point, im->normal, 105, 0, 0, 100, 3);
			break;

		case FXM_GLASS:
			if (!FX_Limit(&fx_lim_glass, 300))
			{
				break;
			}

			FX_DustPuffs(im, d, power, 2, (power <= 2) ? 1 : 2);
			FX_GlassShards(im, 10, FX_irand(4, 6));
			FX_PlaySound(84, im->point, 0.6f, 1.0f, -1, 5, 0);
			break;

		case FXM_LITPANEL:
			FX_ImpactNamed(im, "environ/glass_computer");
			FX_GlassShards(im, 10, FX_irand(4, 6));
			FX_ImpactChunks(im, 18, 6, 50, -1);
			FX_PlaySound(84, im->point, 0.6f, 1.0f, -1, 5, 0);
			break;

		case FXM_VENDING:
			if (!shot || (cl.time - fx_lim_canburst < 3000) || !FX_Limit(&fx_lim_vending, 300))
			{
				break;
			}

			VectorMA(im->point, 5, im->normal, org);
			FX_SparkEmitter(org, im->normal, (float)FX_irand(4000, 8000));
			FX_ImpactNamed(im, "environ/wallspark");

			if (FX_crand() % 20 < 5)
			{
				FX_SodaCans(org, im->normal, 50, FX_irand(2, 4));
				fx_lim_canburst = cl.time;
			}

			if (FX_crand() % 20 < 5)
			{
				FX_SparkShower(org, im->normal);
			}

			break;

		case FXM_PAPER:
			FX_PlaySound(39, im->point, 0.6f, 1.0f, -1, 5, 0);
			break;

		case FXM_PAPERBURST:
			VectorCopy(im->point, org);
			org[2] += 5;
			n = FX_irand(4, 8);
			FX_BlowingPaper(org, n);
			break;
	}
}

/* ---- impact chunks and glass shards (SPEC-401 Impact chunks) ------------------------ */

static const char *fx_wall_skins[11] = {
	"lbrown", "dbrown", "lgrey", "lgrey", "dgrey", "lbrown", "dbrown", "white", "green", "red",
	"black"
};

static void
FX_Chunk(const fx_impact_t *im, int model, const char *skin, float q)
{
	vec3_t right, up, org, vel, avel, zero = {0, 0, 0};
	fx_debris_t *dbr;
	int i;

	FX_Perpendicular(im->normal, right);
	CrossProduct(im->normal, right, up);
	VectorMA(im->point, FX_flrand(20, 40), im->normal, org);
	VectorScale(im->normal, FX_flrand(0.1f * q, 2.0f * q), vel);
	VectorMA(vel, FX_flrand(-q, q), right, vel);
	VectorMA(vel, FX_flrand(-q, q), up, vel);
	vel[2] += FX_flrand(100, 300);

	for (i = 0; i < 3; i++)
	{
		avel[i] = FX_flrand(-200, 200);
	}

	dbr = FX_AddDebris(model, org, zero, vel, avel, -800, 6);

	if (dbr)
	{
		dbr->skin = skin;
		dbr->settle = true;
		dbr->part = FX_irand(0, FX_DebrisParts(model));
	}
}

void
FX_ImpactChunks(const fx_impact_t *im, int material, int count, float scale, int model_override)
{
	int i, model;
	const char *skin;

	if ((material >= 14) && (material <= 24))
	{
		model = 14;
		skin = fx_wall_skins[material - 14];
	}
	else if (material == 37)
	{
		model = 1;
		skin = "glass";
	}
	else if (material == 40)
	{
		model = 11;
		skin = "white";
	}
	else
	{
		return; /* no chunk entry */
	}

	if (model_override >= 0)
	{
		model = model_override;
		skin = fx_wall_skins[18 - 14];
	}

	if (cl.time - fx_chunk_time[material] < 200)
	{
		return; /* a burst of this material less than 200 ms ago */
	}

	fx_chunk_time[material] = cl.time;
	FX_TraceLog("  impact chunks x%d material %d scale %.0f\n", count, material, scale);

	for (i = 0; i < count; i++)
	{
		FX_Chunk(im, model, skin, scale);
	}
}

void
FX_GlassShards(const fx_impact_t *im, float scale, int count)
{
	int i;

	FX_TraceLog("  glass shards x%d\n", count);

	for (i = 0; i < count; i++)
	{
		FX_Chunk(im, 1, "glass", scale);
	}
}

/* ---- spark emitter (SPEC-401) ----------------------------------------------------------- */

static void
FX_SparkEmitterFire(fx_part_t *t, float f)
{
	vec3_t p, a, b, n;
	fx_puff_t pf;
	int i, count;

	(void)f;
	VectorCopy(t->end, n);
	FX_Perpendicular(n, p);

	if (FX_crand() % 2 == 0)
	{
		VectorMA(t->pos, (float)FX_irand(-15, -10), p, a);
		VectorMA(a, FX_flrand(4, 8), n, a);
		VectorMA(t->pos, (float)FX_irand(10, 15), p, b);
		VectorMA(b, FX_flrand(4, 8), n, b);
		FX_ElectricArc(a, b, 6, false);
		return;
	}

	FX_PuffInit(&pf);
	pf.sprite = 16;
	pf.blend = 1;
	pf.life_ms = 500;
	pf.w = pf.h = 20;
	pf.rot = FX_RandRot(); /* as type 10 (SPEC-AMEND-005 I5.15) */
	pf.r = FX_irand(120, 170);
	pf.g = FX_irand(150, 200);
	pf.b = FX_irand(220, 250);
	pf.a = 250;
	pf.grow = -40;
	FX_Puff(t->pos, &pf);
	CL_SoF_AddLight(0, t->pos, 128, 128, 160, 128, 500, 128);

	count = FX_irand(8, 15);

	for (i = 0; i < count; i++)
	{
		FX_SparkParticle(t->pos, n, 32, 160, FX_flrand(0.5f, 1.0f), 175, -300, 15, 1, 200, 250);
	}

	FX_PlaySound(FX_crand() % 2, t->pos, 0.6f, 1.0f, 0, 0, 0);
}

void
FX_SparkEmitter(const vec3_t org, const vec3_t n, float life_ms)
{
	fx_part_t *t = FX_Timer(org, FX_SparkEmitterFire, 1000, 0.7f, life_ms);

	if (t)
	{
		VectorCopy(n, t->end);
	}
}

/* ---- footsteps and entity events (SPEC-401 Entity events) -------------------------------- */

typedef struct
{
	int power;
	float volume, alpha;
	int kind; /* 0 left, 1 right, 2 landing (both feet), 3 landing centred */
	int sprite;
} fx_step_t;

static const fx_step_t fx_steps[15] = {
	{0}, {0}, {0},
	/* 3 */ {0, 0.7f, 0.3f, 0, 5},
	/* 4 */ {0, 0.7f, 0.3f, 1, 6},
	/* 5 */ {2, 1.0f, 0.8f, 0, 5},
	/* 6 */ {2, 1.0f, 0.8f, 1, 6},
	/* 7 */ {3, 1.0f, 0.8f, 2, 18},
	/* 8 */ {6, 1.0f, 1.0f, 2, 18},
	/* 9 */ {9, 1.0f, 1.0f, 2, 18},
	/* 10 */ {4, 0.8f, 0.8f, 3, 18},
	/* 11 */ {6, 1.0f, 1.0f, 3, 18},
	/* 12 */ {9, 1.0f, 1.0f, 3, 18},
	/* 13 */ {2, 1.0f, 0.8f, 0, 5},
	/* 14 */ {2, 1.0f, 0.8f, 1, 6},
};

/* one foot: side -1 left, +1 right, 0 centred; sound: 0 none, 1 step, 2 landing,
   3 landing + Fall3 */
static void
FX_Foot(int ent, const entity_state_t *s, int side, float volume, float alpha, int sprite,
		int power, int sound)
{
	vec3_t org, right, start, end, v, zero = {0, 0, 0};
	trace_t tr;
	int hit, snd;
	fx_impact_t im;
	float dist, vol;

	VectorCopy(s->origin, org);
	AngleVectors(s->angles, NULL, right, NULL);

	if (side)
	{
		VectorMA(org, 5.0f * side, right, org);
	}

	VectorCopy(org, start);
	start[2] += 10;
	VectorCopy(org, end);
	end[2] -= 64;
	FX_Trace(start, NULL, NULL, end, FXTR_BRUSHES, &tr, &hit);

	if (tr.fraction >= 0.99f)
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

	if (im.material > 42)
	{
		return;
	}

	VectorSubtract(im.point, cl.refdef.vieworg, v);
	dist = VectorLength(v);
	vol = FX_FootVolume(volume, dist);

	if (sound == 1)
	{
		snd = (im.material == 35) ? 37 : fx_materials[im.material].footstep + FX_crand() % 4;
		FX_PlaySound(snd, im.point, vol, 1.0f, -1, 0, 0);
	}
	else if (sound >= 2)
	{
		FX_PlaySound(fx_materials[im.material].landing, im.point, vol, 1.0f, -1, 0, 0);

		if (sound == 3)
		{
			FX_PlaySound(101, im.point, vol, 1.0f, -1, 0, 0);
		}
	}

	im.markmode = 0;
	im.hole = sprite;
	im.alphafactor = alpha;
	im.yaw = FX_DEG2RAD(s->angles[YAW]);
	FX_MarkAndSound(&im, 0);
	FX_MaterialEffect(&im, zero, power);
}

static void
FX_Footstep(int ent, int id)
{
	const entity_state_t *s = FX_EntityCurrent(ent);
	const fx_step_t *st = &fx_steps[id];
	const char *maxclients;

	if (!s || !cl_footsteps || !cl_footsteps->value || !cl.refresh_prepped)
	{
		return;
	}

	maxclients = CL_SoF_ConfigString(31);

	if ((id == 3 || id == 4) && strcmp(maxclients, "1") && !FX_IsLocalPlayer(ent))
	{
		return; /* multiplayer: walking steps of others are not played */
	}

	FX_TraceLog("event %d (footstep) entity %d\n", id, ent);

	if (st->kind == 2)
	{
		/* landings 7..9: two footprints */
		FX_Foot(ent, s, -1, fx_steps[3].volume, 0.3f, 5, 0, (id == 7) ? 2 : 3);
		FX_Foot(ent, s, 1, fx_steps[4].volume, 0.3f, 6, 0, 0);
		return;
	}

	FX_Foot(ent, s, (st->kind == 3) ? 0 : ((st->kind == 0) ? -1 : 1), st->volume, st->alpha,
			st->sprite, st->power, (st->kind == 3) ? 3 : 1);
}

void
FX_EntityEvent(int ent, int event, int parm)
{
	static const float shake[5] = {7, 17, 35, 75, 130};

	if ((event >= 3) && (event <= 14))
	{
		FX_Footstep(ent, event);
		return;
	}

	if ((event >= 16) && (event <= 20))
	{
		if (FX_IsLocalPlayer(ent))
		{
			FX_TraceLog("event %d (view shake +%.0f, decay %d) entity %d\n", event,
					shake[event - 16], parm * 4, ent);
			CLFX_AddViewShake(shake[event - 16], (float)(parm * 4));
		}

		return;
	}

	if (event == 21)
	{
		if (FX_IsLocalPlayer(ent))
		{
			FX_TraceLog("event 21 (bullet crack) entity %d\n", ent);
			FX_PlaySound(102 + FX_irand(0, 2), NULL, 1.0f, 0.0f, ent, 1, 0);
		}

		return;
	}

	if (event == 22)
	{
		FX_TraceLog("event 22 (model tint reset) entity %d\n", ent);
		return; /* the model tint becomes (1, 1, 1, 1): Ghoul entities */
	}

	FX_TraceLog("event %d entity %d parm %d: nothing\n", event, ent, parm);
}
