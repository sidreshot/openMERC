/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Client effects: console command om_fxselftest (OpenMerc only). Runs the
 * numeric test vectors of SPEC-400/401, one emitter end to end
 * (effects/canthear.eft, test vector 4), and feeds every one of the 65
 * temp-entity types and svc_effect a synthetic payload built from the
 * SPEC-201 tables to check that each handler consumes exactly its payload.
 * Needs no map and no server; prints PASS/FAIL lines and a summary.
 *
 * Implements: SPEC-400 (Test vectors 4-10), SPEC-401 (Test vectors 1, 2, 4,
 * Dispatch item 1), SPEC-201 s.1 / s.5 (payload sizes), SPEC-AMEND-005 E
 * (Verify items of I5.1, I5.2, I5.3, I5.4, I5.5, I5.7, I5.12, I5.15, I5.18)
 */

#include "fx_local.h"

static int fx_ok, fx_bad;

static void
FX_Check(qboolean cond, const char *fmt, ...)
{
	va_list ap;
	char text[512];

	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);

	if (cond)
	{
		fx_ok++;
		Com_Printf("PASS %s\n", text);
	}
	else
	{
		fx_bad++;
		Com_Printf("FAIL %s\n", text);
	}
}

static qboolean
FX_Near(float a, float b, float eps)
{
	return fabs(a - b) <= eps;
}

/* SPEC-201 s.1 payload formats: p pos, d dir, b byte, s short, l long,
   I impact (kind 1 here), N byte count + count x (short, byte) */
static const char *fx_te_payload[65] = {
	"pd", "p", "pbbbb", "I", "pl", "", "Ipbbb", "Ipbbb", "pbd", "bppbb",
	"pd", "psb", "p", "I", "pd", "pl", "", "pl", "pdbbbbbbb", "I",
	"p", "p", "", "pb", "ppN", "pp", "pb", "ppll", "pl", "I",
	"I", "pd", "pdb", "pb", "pp", "sssb", "sssss", "ss", "p", "s",
	"pb", "sssb", "sss", "pp", "pb", "pbbbb", "pbb", "ps", "ps", "pd",
	"ppss", "p", "ps", "pl", "p", "pbb", "pbb", "pp", "p", "p",
	"bbbpp", "bbbpp", "bbbpp", "bbpp", "ppbbb"
};

static void
FX_WriteImpact(sizebuf_t *sb, int kind)
{
	vec3_t v = {0, 0, 0}, n = {0, 0, 1000};

	MSG_WriteByte(sb, kind);

	switch (kind)
	{
		case 0:
			MSG_WritePos(sb, v);
			MSG_WritePos(sb, n);
			break;
		case 1:
			MSG_WritePos(sb, v);
			break;
		case 2:
			MSG_WritePos(sb, v);
			MSG_WritePos(sb, n);
			MSG_WriteShort(sb, 1);
			MSG_WriteByte(sb, 37);
			break;
		default:
			MSG_WritePos(sb, v);
			MSG_WritePos(sb, v);
			MSG_WriteByte(sb, 35);
			break;
	}
}

static void
FX_BuildPayload(sizebuf_t *sb, const char *f, int impactkind)
{
	vec3_t v = {16, 8, 4};
	int i;

	for (; *f; f++)
	{
		switch (*f)
		{
			case 'p': MSG_WritePos(sb, v); break;
			case 'd': MSG_WriteByte(sb, 7); break;
			case 'b': MSG_WriteByte(sb, 2); break;
			case 's': MSG_WriteShort(sb, 3); break;
			case 'l': MSG_WriteLong(sb, 0x7F102030); break;
			case 'I': FX_WriteImpact(sb, impactkind); break;
			case 'N':
				MSG_WriteByte(sb, 2);

				for (i = 0; i < 2; i++)
				{
					MSG_WriteShort(sb, i + 1);
					MSG_WriteByte(sb, 1);
				}

				break;
		}
	}
}

/* runs handler(type) on the given bytes as the current net_message */
static int
FX_RunPayload(sizebuf_t *sb, int type, qboolean effect)
{
	sizebuf_t saved = net_message;
	int left;

	net_message = *sb;
	net_message.readcount = 0;

	if (effect)
	{
		CLFX_ParseEffect();
	}
	else
	{
		CLFX_ParseTempEntity(type);
	}

	left = net_message.cursize - net_message.readcount;
	net_message = saved;
	return left;
}

static int
FX_CountParticles(float d1, int *total)
{
	int i, n = 0, t = 0;

	for (i = 0; i < FX_MAX_PARTICLES; i++)
	{
		if (fx_parts[i].kind != FXK_FREE)
		{
			t++;

			if ((d1 < 0) || FX_Near(fx_parts[i].d1, d1, 0.01f))
			{
				n++;
			}
		}
	}

	if (total)
	{
		*total = t;
	}

	return n;
}

static void
FX_TestVectors(void)
{
	vec3_t v, n = {0, 0, 1};
	float a, t, now;
	int steps, i;

	/* SPEC-400 test vector 5 */
	FX_Check(FX_LodCount(10, 393216.0f) == 6, "LOD: N=10 at 512^2*1.5 -> %d (want 6)",
			FX_LodCount(10, 393216.0f));

	/* test vector 6 */
	for (i = 0; i < 4; i++)
	{
		a = FX_RAD2DEG(FX_RingAngle(i, 4, 0));
		FX_Check(FX_Near(a, 90.0f * (i + 1), 0.01f), "ring element %d at %.2f deg (want %d)", i, a,
				90 * (i + 1));
	}

	/* test vector 7 */
	VectorSet(v, 100, 0, -200);
	FX_Bounce(v, n, 0.7f);
	FX_Check(FX_Near(v[0], 70, 0.001f) && FX_Near(v[1], 0, 0.001f) && FX_Near(v[2], 112, 0.001f),
			"bounce (100,0,-200) -> (%.2f %.2f %.2f) (want 70 0 112)", v[0], v[1], v[2]);
	VectorSet(v, 20, 0, -25);
	FX_Bounce(v, n, 0.7f);
	FX_Check(!v[0] && !v[1] && !v[2], "bounce (20,0,-25) -> (%.2f %.2f %.2f) (want rest)", v[0],
			v[1], v[2]);

	/* test vector 8 */
	t = 0.25f + 0.01f;
	FX_Check((int)FX_StyleAlpha(2, t, 0, 200) == 104, "alpha style 2 at 1/4 life: %d (want 104)",
			(int)FX_StyleAlpha(2, t, 0, 200));
	t = 0.75f + 0.01f;
	FX_Check((int)FX_StyleAlpha(2, t, 0, 200) == 96, "alpha style 2 at 3/4 life: %d (want 96)",
			(int)FX_StyleAlpha(2, t, 0, 200));

	/* test vector 9 (puddle) */
	a = FX_DecalGrowSize(2, 25, -3.1f, 3.0f, 1.0f);
	FX_Check(FX_Near(a, 12.767f, 0.001f), "decal growth puddle size(1 s) = %.4f (want 12.767)", a);
	a = FX_DecalGrowSize(2, 25, -3.1f, 3.0f, 3.0f);
	FX_Check(FX_Near(a, 25, 0.001f), "decal growth puddle size(3 s) = %.4f (want 25)", a);
	a = FX_DecalGrowSize(2, 25, -3.1f, 3.0f, 2.9999f);
	FX_Check(FX_Near(a, 25, 0.01f), "decal growth puddle size(3 s - eps) = %.4f (want 25)", a);

	/* test vector 10 */
	now = 10.0f;
	steps = FX_AdvanceClock(&now, 10.05f, 1.0f / 60.0f, NULL);
	FX_Check((steps == 3) && FX_Near(now, 10.05f, 0.00001f),
			"clock 10.000 -> 10.050 at 60 Hz: %d steps, fx time %.5f (want 3, 10.05000)", steps, now);
	now = 5.0f;
	steps = FX_AdvanceClock(&now, 10.0f, 1.0f / 60.0f, NULL);
	FX_Check(steps == 12, "clock 5 s behind: %d steps (want 12 = 0.2 s)", steps);

	/* SPEC-401 test vector 2: volume and hole size for power 10 */
	FX_Check(FX_Near(FX_ImpactVolume(10), 0.9333f, 0.0001f), "impact volume power 10 = %.4f (want 0.9333)",
			FX_ImpactVolume(10));
	FX_Check(FX_Near(FX_ImpactVolume(0), 0.6f, 0.0001f), "impact volume power 0 = %.4f (want 0.6)",
			FX_ImpactVolume(0));

	/* SPEC-401 test vector 4: footstep volumes */
	FX_Check(FX_Near(FX_FootVolume(1.0f, 25), 1.0f, 0.0001f), "footstep id 5 at 25 units: %.3f (want 1.0)",
			FX_FootVolume(1.0f, 25));
	FX_Check(FX_Near(FX_FootVolume(1.0f, 100), 0.5f, 0.0001f), "footstep id 5 at 100 units: %.3f (want 0.5)",
			FX_FootVolume(1.0f, 100));
}

static void
FX_TestCanthear(void)
{
	fx_params_t p;
	fx_def_t *def;
	fx_part_t *q = NULL;
	int i, count = 0, k;
	float dt = 1.0f / 60.0f;

	FX_ClearParticles();
	FX_ClearRunning();
	fx_now = 100.0f;
	def = FX_FindEffect("canthear");

	if (!def || !def->loaded)
	{
		FX_Check(false, "canthear: effects/canthear.eft not loaded");
		return;
	}

	FX_DefaultParams(&p);
	FX_StartEffect(def, &p, FXS_GAME);

	for (k = 0; k <= 45; k++)
	{
		FX_FreeDeadParticles(); /* the per-frame freeing pass, as CLFX_Frame */
		FX_UpdateRunning();
		FX_RunParticles(dt);

		if (k < 45)
		{
			fx_now += dt;
		}
	}

	for (i = 0; i < FX_MAX_PARTICLES; i++)
	{
		if ((fx_parts[i].kind != FXK_FREE) && fx_parts[i].desc)
		{
			count++;
			q = &fx_parts[i];
		}
	}

	FX_Check(count == 1, "canthear: %d particle(s) (want exactly 1)", count);

	if (!q)
	{
		return;
	}

	FX_Check(!strcmp(q->q.texname, "textures/sprites/snow"), "canthear: sprite %s", q->q.texname);
	FX_Check(FX_Near(q->d1, 8, 0.001f) && FX_Near(q->d2, 8, 0.001f), "canthear: size %.2f x %.2f (want 8 x 8)",
			q->d1, q->d2);
	FX_Check((q->q.flags & (AQ_BILLBOARD | AQ_ADDITIVE)) == (AQ_BILLBOARD | AQ_ADDITIVE),
			"canthear: flags 0x%04x (billboard + additive)", q->q.flags);
	FX_Check(FX_Near(q->die - q->spawn, 1.5f, 0.0001f), "canthear: lifetime %.3f (want 1.5)",
			q->die - q->spawn);
	FX_Check(q->alpha == (int)(155 * sinf((float)M_PI * (0.5f + 0.01f))),
			"canthear: alpha at 0.75 s = %d (want %d)", q->alpha,
			(int)(155 * sinf((float)M_PI * (0.5f + 0.01f))));
	FX_Check((q->rgb[0] == 255) && (q->rgb[1] == 127) && (q->rgb[2] == 30),
			"canthear: colour at 0.75 s = (%d %d %d) (want 255 127 30)", q->rgb[0], q->rgb[1],
			q->rgb[2]);
	FX_Check(fabs(q->vel[0]) <= 10 && fabs(q->vel[1]) <= 10 && fabs(q->vel[2]) <= 10,
			"canthear: velocity (%.1f %.1f %.1f) within +-10", q->vel[0], q->vel[1], q->vel[2]);

	FX_ClearParticles();
	FX_ClearRunning();
}

static void
FX_TestPayloads(void)
{
	byte data[1400];
	sizebuf_t sb;
	int type, left, kinds, k, total, n20, n30;
	vec3_t v = {0, 0, 0};

	for (type = 0; type <= 64; type++)
	{
		kinds = strchr(fx_te_payload[type], 'I') ? 4 : 1;

		for (k = 0; k < kinds; k++)
		{
			SZ_Init(&sb, data, sizeof(data));
			FX_BuildPayload(&sb, fx_te_payload[type], (kinds == 4) ? k : 1);
			left = FX_RunPayload(&sb, type, false);
			FX_Check(left == 0, "temp entity %d%s: payload %d bytes, %d left", type,
					(kinds == 4) ? va(" impact kind %d", k) : "", sb.cursize, left);
		}
	}

	/* SPEC-401 test vector 1: TE 46 size 100 sound 1 */
	FX_ClearParticles();
	SZ_Init(&sb, data, sizeof(data));
	MSG_WritePos(&sb, v);
	MSG_WriteByte(&sb, 100);
	MSG_WriteByte(&sb, 1);
	FX_RunPayload(&sb, 46, false);
	n30 = FX_CountParticles(30, &total);
	n20 = FX_CountParticles(20, NULL);
	FX_Check((n30 == 1) && (n20 == 8) && (total == 9),
			"TE 46 size 100: %d flash 30x30, %d fireballs 20x20, %d particles (want 1, 8, 9)", n30, n20,
			total);

	/* TE 0: flash + 14 sparks */
	FX_ClearParticles();
	SZ_Init(&sb, data, sizeof(data));
	MSG_WritePos(&sb, v);
	MSG_WriteByte(&sb, 5);
	FX_RunPayload(&sb, 0, false);
	FX_CountParticles(-1, &total);
	FX_Check(total == 15, "TE 0: %d particles (want 1 flash + 14 sparks)", total);

	/* svc_effect with every field present (SPEC-201 s.5) */
	SZ_Init(&sb, data, sizeof(data));
	MSG_WriteByte(&sb, 1);
	MSG_WriteByte(&sb, 0x04 | 0x08 | 0x40 | 0x80);
	MSG_WriteShort(&sb, 1);
	MSG_WriteShort(&sb, 2);
	MSG_WriteShort(&sb, 3);
	MSG_WriteByte(&sb, 0xFF);
	MSG_WriteShort(&sb, 256);
	MSG_WriteByte(&sb, 4);
	MSG_WritePos(&sb, v);
	MSG_WritePos(&sb, v);
	MSG_WritePos(&sb, v);
	MSG_WritePos(&sb, v);
	MSG_WriteShort(&sb, 128);
	MSG_WriteByte(&sb, 1);
	left = FX_RunPayload(&sb, 0, true);
	FX_Check(left == 0, "svc_effect bolt + ghoul + all extensions: %d bytes, %d left", sb.cursize, left);

	SZ_Init(&sb, data, sizeof(data));
	MSG_WriteByte(&sb, 1);
	MSG_WriteByte(&sb, 0x01);
	MSG_WritePos(&sb, v);
	left = FX_RunPayload(&sb, 0, true);
	FX_Check(left == 0, "svc_effect origin only: %d bytes, %d left", sb.cursize, left);

	FX_ClearParticles();
	FX_ClearDebris();
	FX_ClearDecals();
	FX_ClearRunning();
}

static fx_part_t *
FX_FirstParticle(int kind)
{
	int i;

	for (i = 0; i < FX_MAX_PARTICLES; i++)
	{
		if ((fx_parts[i].kind != FXK_FREE) && ((kind < 0) || (fx_parts[i].kind == kind)))
		{
			return &fx_parts[i];
		}
	}

	return NULL;
}

/* TE 6 with an impact of kind 3 (material from the payload) */
static void
FX_RunWeaponImpact(int material, int style, int debris)
{
	byte data[64];
	sizebuf_t sb;
	vec3_t v = {0, 0, 0};

	SZ_Init(&sb, data, sizeof(data));
	MSG_WriteByte(&sb, 3);
	MSG_WritePos(&sb, v);
	MSG_WritePos(&sb, v);
	MSG_WriteByte(&sb, material);
	MSG_WritePos(&sb, v);
	MSG_WriteByte(&sb, 10);
	MSG_WriteByte(&sb, style);
	MSG_WriteByte(&sb, debris);
	FX_RunPayload(&sb, 6, false);
}

/* the Verify items of SPEC-AMEND-005 E (I5.1-I5.18) that need no map */
static void
FX_TestAmend005(void)
{
	byte data[256];
	sizebuf_t sb;
	vec3_t v = {0, 0, 0}, e = {100, 0, 0}, pre;
	fx_part_t *p;
	float dt = 1.0f / 60.0f, maxx;
	int i, steps, n, total, lines;
	char savedq[32], savedcap[32];
	qboolean ok;

	FX_ClearTempState();

	/* I5.1: TE 45 alpha 155, rate -1.6667 x 155: loses 5 per step, gone (< 2) after 31 steps */
	FX_ClearParticles();
	SZ_Init(&sb, data, sizeof(data));
	MSG_WritePos(&sb, v);
	MSG_WriteByte(&sb, 200);
	MSG_WriteByte(&sb, 200);
	MSG_WriteByte(&sb, 200);
	MSG_WriteByte(&sb, 155);
	FX_RunPayload(&sb, 45, false);
	p = FX_FirstParticle(FXK_BILLBOARD);
	steps = 0;

	while (p && (p->alpha >= 2) && (steps < 200))
	{
		FX_RunParticles(dt);
		fx_now += dt;
		steps++;
	}

	FX_Check(steps == 31, "I5.1 TE 45 alpha 155: below 2 after %d steps (want 31)", steps);

	/* I5.3: TE 11 size 8: +0x0c (along up) 12, +0x10 8 */
	FX_ClearParticles();
	SZ_Init(&sb, data, sizeof(data));
	MSG_WritePos(&sb, v);
	MSG_WriteShort(&sb, 0x0FFF);
	MSG_WriteByte(&sb, 8);
	FX_RunPayload(&sb, 11, false);
	p = FX_FirstParticle(FXK_BILLBOARD);
	FX_Check(p && FX_Near(p->d1, 12, 0.001f) && FX_Near(p->d2, 8, 0.001f),
			"I5.3 TE 11 size 8: %.2f x %.2f (want 12 along up x 8)", p ? p->d1 : -1, p ? p->d2 : -1);

	/* I5.4: style above 42 -> nothing; material 60 on a kind-3 impact -> nothing */
	FX_ClearParticles();
	FX_ClearDebris();
	FX_RunWeaponImpact(17, 50, 1);
	FX_CountParticles(-1, &total);
	FX_Check((total == 0) && !FX_DebrisSlotAlive(0), "I5.4 TE 6 style 50: %d particles, debris %d (want none)",
			total, FX_DebrisSlotAlive(0));
	FX_RunWeaponImpact(60, 1, 1);
	FX_CountParticles(-1, &total);
	FX_Check((total == 0) && !FX_DebrisSlotAlive(0), "I5.4 TE 6 style 1 material 60: %d particles, debris %d (want none)",
			total, FX_DebrisSlotAlive(0));

	/* I5.5: style 1 with the debris flag on a tiled wall: exactly one chunk, none again within 0.2 s */
	FX_ClearDebris();
	FX_ClearTempState();
	FX_RunWeaponImpact(17, 1, 1);
	FX_Check(FX_DebrisSlotAlive(0) && !FX_DebrisSlotAlive(1), "I5.5 TE 6 style 1 debris, material 17: one chunk");
	FX_RunWeaponImpact(17, 1, 1);
	FX_Check(!FX_DebrisSlotAlive(1), "I5.5 second hit within 200 ms: no chunk");

	/* I5.7: arc of length 100 with L = 4: 24 segments, ending about two steps before the end */
	FX_ClearParticles();
	FX_ElectricArc(v, e, 4, false);
	lines = 0;
	maxx = -1000;
	ok = true;

	for (i = 0; i < FX_MAX_PARTICLES; i++)
	{
		if (fx_parts[i].kind == FXK_LINE)
		{
			lines++;
			maxx = fx_parts[i].end[0] > maxx ? fx_parts[i].end[0] : maxx;
			ok = ok && !strcmp(fx_parts[i].q.texname, "textures/sprites/lit") &&
				!(fx_parts[i].q.flags & AQ_LIT) && (fx_parts[i].q.flags & AQ_ADDITIVE);
		}
	}

	FX_Check(lines == 24, "I5.7 arc length 100, L 4: %d segments (want 24)", lines);
	FX_Check((100 - maxx > 3) && (100 - maxx < 13), "I5.7 arc stops %.1f units before its end (want about 8)",
			100 - maxx);
	FX_Check(ok, "I5.7 arc segments: sprite lit, additive, not world-lit");

	/* I5.12: ring 20, cap 4 (+1), 20 chunks: the first and the last four remain */
	Q_strlcpy(savedq, cl_max_debris->string, sizeof(savedq));
	Q_strlcpy(savedcap, fx_maxdebrisonscreen->string, sizeof(savedcap));
	Cvar_Set("cl_max_debris", "20");
	Cvar_Set("fx_maxdebrisonscreen", "4");
	FX_ClearDebris();

	for (i = 0; i < 20; i++)
	{
		FX_AddDebris(7, v, v, v, v, 0, 6);
	}

	FX_SubmitDebris();
	ok = FX_DebrisSlotAlive(0);

	for (i = 1; i < 20; i++)
	{
		ok = ok && (FX_DebrisSlotAlive(i) == (i >= 16));
	}

	FX_Check(ok, "I5.12 debris visit order: chunk 1 and chunks 17-20 remain");
	Cvar_Set("cl_max_debris", savedq);
	Cvar_Set("fx_maxdebrisonscreen", savedcap);
	FX_ClearDebris();

	/* I5.15: the 15 flashbang flares are drawn at alpha 250 */
	FX_ClearParticles();
	SZ_Init(&sb, data, sizeof(data));
	MSG_WritePos(&sb, v);
	FX_RunPayload(&sb, 20, false);
	n = 0;

	for (i = 0; i < FX_MAX_PARTICLES; i++)
	{
		if ((fx_parts[i].kind == FXK_BILLBOARD) && FX_Near(fx_parts[i].d1, 32, 0.01f) &&
			(fx_parts[i].alpha == 250) && (fx_parts[i].rgb[0] == 250))
		{
			n++;
		}
	}

	FX_Check(n == 15, "I5.15 TE 20: %d flares 32 x 32 (250, 250, 250, 250) (want 15)", n);

	/* I5.18: an Instant effect with delay per burst 1.0 s and duration 0.1 s keeps its slot until the next
	   burst time (about 1 s), not only until its particle dies (0.2 s) */
	{
		static fx_def_t def18;
		fx_params_t prm;
		fx_eft_emitter_t *e;
		int slot;
		float t0, gone = -1;

		memset(&def18, 0, sizeof(def18));
		Q_strlcpy(def18.name, "om_test_i5_18", sizeof(def18.name));
		def18.loaded = true;
		def18.eft.count = 1;
		def18.eft.prims[0].type = FXP_EMITTER;
		e = &def18.eft.prims[0].u.emitter;
		e->delay.min = e->delay.max = 1.0f;
		e->duration.min = e->duration.max = 0.1f;
		e->elements = 1;
		e->p.life.min = e->p.life.max = 0.2f;
		e->p.rgba[3].min = e->p.rgba[3].max = 200;
		Q_strlcpy(e->p.graphic, "textures/sprites/flare", sizeof(e->p.graphic));

		FX_ClearParticles();
		FX_ClearRunning();
		FX_DefaultParams(&prm);
		t0 = fx_now;
		slot = FX_StartEffect(&def18, &prm, FXS_GAME);

		for (steps = 0; (slot >= 0) && (steps < 180); steps++)
		{
			FX_FreeDeadParticles();
			FX_UpdateRunning();
			FX_RunParticles(dt);

			if (!fx_running[slot].inuse)
			{
				gone = fx_now - t0;
				break;
			}

			fx_now += dt;
		}

		FX_Check((gone > 0.95f) && (gone < 1.1f), "I5.18 effect slot kept for %.2f s after its single burst (want about 1.0)",
				gone);
		FX_ClearRunning();
	}

	/* I5.2: the picture shows the referenced records after the frame's steps */
	FX_ClearParticles();
	SZ_Init(&sb, data, sizeof(data));
	MSG_WritePos(&sb, v);
	MSG_WriteByte(&sb, 10);
	FX_RunPayload(&sb, 26, false);
	FX_BeginScene();
	FX_SubmitParticles();
	p = FX_FirstParticle(FXK_BILLBOARD);
	VectorClear(pre);

	if (p)
	{
		VectorCopy(p->q.origin, pre);
	}
	FX_RunParticles(dt);
	FX_RefreshScene();
	FX_Check(p && VectorCompare(p->q.origin, p->pos) && !VectorCompare(pre, p->pos),
			"I5.2 submitted puff drawn at its post-step position");
	FX_BeginScene();

	FX_ClearParticles();
	FX_ClearDebris();
	FX_ClearDecals();
	FX_ClearTempState();
}

/* OpenMerc pool admission (docs/DEVIATIONS.md): with all 256 running slots taken by far
   world effects the local player's own effect still starts, a far world request is
   dropped, a near one replaces the farthest; evicted effects leave no owner links */
static void
FX_TestPool(void)
{
	static fx_def_t defp;
	fx_params_t prm;
	fx_eft_emitter_t *e;
	int i, slot, slot2, slot3, ev0, dw0, owners = 0;

	memset(&defp, 0, sizeof(defp));
	Q_strlcpy(defp.name, "om_test_pool", sizeof(defp.name));
	defp.loaded = true;
	defp.eft.count = 1;
	defp.eft.prims[0].type = FXP_EMITTER;
	e = &defp.eft.prims[0].u.emitter;
	e->delay.min = e->delay.max = 0.05f;
	e->duration.min = e->duration.max = 10.0f;
	e->elements = 1;
	e->p.life.min = e->p.life.max = 2.0f;
	e->p.rgba[3].min = e->p.rgba[3].max = 200;
	Q_strlcpy(e->p.graphic, "textures/sprites/flare", sizeof(e->p.graphic));

	FX_ClearParticles();
	FX_ClearRunning();

	for (i = 0; i < FX_MAX_RUNNING; i++)
	{
		FX_DefaultParams(&prm);
		VectorCopy(cl.refdef.vieworg, prm.origin);
		prm.origin[0] += 3000 + i;
		FX_StartEffect(&defp, &prm, FXS_GAME);
	}

	FX_UpdateRunning(); /* first bursts: the effects own particles */
	ev0 = fx_pool_evicted;
	dw0 = fx_pool_drop_world;

	FX_DefaultParams(&prm);
	prm.mode = FXA_ENTITY;
	prm.ent = cl.playernum + 1;
	slot = FX_StartEffect(&defp, &prm, FXS_GAME);
	FX_Check((slot >= 0) && fx_running[slot].priority && (fx_pool_evicted == ev0 + 1),
			"pool full: own effect started in slot %d (evicted %d)", slot, fx_pool_evicted - ev0);
	FX_Check((slot >= 0) && (slot == FX_MAX_RUNNING - 1),
			"pool full: the farthest world effect (slot 255) was replaced (got %d)", slot);

	for (i = 0; i < FX_MAX_PARTICLES; i++)
	{
		owners += (fx_parts[i].kind != FXK_FREE) && (fx_parts[i].fx == slot);
	}

	FX_Check(owners == 0, "pool full: no particle of the evicted effect points at the reused slot (%d)", owners);

	FX_DefaultParams(&prm);
	VectorCopy(cl.refdef.vieworg, prm.origin);
	prm.origin[0] += 2900;
	slot2 = FX_StartEffect(&defp, &prm, FXS_GAME);
	FX_Check((slot2 < 0) && (fx_pool_drop_world == dw0 + 1), "pool full: a far world effect is dropped (%d)", slot2);

	VectorCopy(cl.refdef.vieworg, prm.origin);
	slot3 = FX_StartEffect(&defp, &prm, FXS_GAME);
	FX_Check((slot3 >= 0) && !fx_running[slot3].priority, "pool full: a near world effect replaces a far one (%d)",
			slot3);

	FX_ClearParticles();
	FX_ClearRunning();
}


/* Owner meat2: force TE 18 flesh + gore/gibbody in front of the view so the
   debris model/scale path can be traced without hoping an NPC gibs. */
void
FX_MeatDemo_f(void)
{
	sizebuf_t sb;
	byte buf[64];
	vec3_t org, fwd;
	fx_params_t prm;
	int sizes[3] = {1, 3, 4};
	int i;

	if (!cl.frame.valid)
	{
		Com_Printf("om_fxmeatdemo: need an active map\n");
		return;
	}

	VectorCopy(cl.refdef.vieworg, org);
	AngleVectors(cl.refdef.viewangles, fwd, NULL, NULL);
	VectorMA(org, 80, fwd, org);
	org[2] -= 8;

	Com_Printf("om_fxmeatdemo: TE18 flesh sizes 1/3/4 + gore/gibbody at (%.0f %.0f %.0f)\n",
			org[0], org[1], org[2]);

	for (i = 0; i < 3; i++)
	{
		memset(&sb, 0, sizeof(sb));
		sb.data = buf;
		sb.maxsize = sizeof(buf);
		MSG_WritePos(&sb, org);
		MSG_WriteDir(&sb, fwd);
		MSG_WriteByte(&sb, 6);           /* count */
		MSG_WriteByte(&sb, sizes[i]);    /* size class */
		MSG_WriteByte(&sb, 32);          /* flesh chunk type */
		MSG_WriteByte(&sb, 8);           /* ex */
		MSG_WriteByte(&sb, 8);           /* ey */
		MSG_WriteByte(&sb, 8);           /* ez */
		MSG_WriteByte(&sb, 35);          /* material flesh */
		FX_RunPayload(&sb, 18, false);
	}

	FX_DefaultParams(&prm);
	VectorCopy(org, prm.origin);
	VectorCopy(fwd, prm.fwd);
	FX_StartNamed("gore/gibbody", &prm, FXS_GAME);
	FX_StartNamed("gore/mpgsplatter", &prm, FXS_GAME);
	Com_Printf("om_fxmeatdemo: done (om_fxtrace 2 for model/scale lines)\n");
}

void
FX_SelfTest_f(void)
{
	float saved = fx_now;

	fx_ok = fx_bad = 0;
	Com_Printf("---- om_fxselftest ----\n");
	FX_TestVectors();
	FX_TestCanthear();
	FX_TestPayloads();
	FX_TestAmend005();
	FX_TestPool();
	fx_now = saved;
	Com_Printf("om_fxselftest: %d passed, %d failed\n", fx_ok, fx_bad);
}
