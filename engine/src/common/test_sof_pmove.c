/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * SPEC-314 test vectors for the shared player-movement routine
 * (common/sof_pmove.c), on the synthetic brush worlds of the spec's
 * "Setting": convex brushes, Quake II position test and box sweep with
 * the 1/32 epsilon, point contents = OR of brushes strictly containing
 * the point, ground-entity-is-client = entity numbers 1..N.
 *
 * Usage: sofpm_test [-v]   exit code = number of failed checks
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "header/sof_pmove.h"

/* ---- worlds ---------------------------------------------------------------- */

#define MAXPLANES 8
#define MAXBRUSHES 8

typedef struct
{
	int numplanes;
	float n[MAXPLANES][3];
	float d[MAXPLANES];
	int contents;
	int surfflags;
	int ent;
} brush_t;

typedef struct
{
	int numbrushes;
	brush_t b[MAXBRUSHES];
	int nclients;      /* ground-entity-is-client: 1..N */
	int gic_calls;
	int gic_last;
	int emergency, badinitial;
} world_t;

static int entnums[64];

static void
AddPlane(brush_t *b, float nx, float ny, float nz, float d)
{
	b->n[b->numplanes][0] = nx;
	b->n[b->numplanes][1] = ny;
	b->n[b->numplanes][2] = nz;
	b->d[b->numplanes] = d;
	b->numplanes++;
}

static brush_t *
NewBrush(world_t *w, int contents, int ent)
{
	brush_t *b = &w->b[w->numbrushes++];

	memset(b, 0, sizeof(*b));
	b->contents = contents;
	b->ent = ent;
	return b;
}

static brush_t *
AddBox(world_t *w, float x0, float y0, float z0, float x1, float y1, float z1, int contents)
{
	brush_t *b = NewBrush(w, contents, 0);

	AddPlane(b, 1, 0, 0, x1);
	AddPlane(b, -1, 0, 0, -x0);
	AddPlane(b, 0, 1, 0, y1);
	AddPlane(b, 0, -1, 0, -y0);
	AddPlane(b, 0, 0, 1, z1);
	AddPlane(b, 0, 0, -1, -z0);
	return b;
}

static brush_t *
WorldF(world_t *w)
{
	return AddBox(w, -8192, -8192, -64, 8192, 8192, 0, 1);
}

static float
Dot3(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void
W_Trace(void *ctx, sofpm_trace_t *tr, const float *start, const float *mins, const float *maxs,
		const float *end, int mask)
{
	world_t *w = ctx;
	int i, j, k;
	int lastent = 0;

	memset(tr, 0, sizeof(*tr));
	tr->fraction = 1;

	if ((start[0] == end[0]) && (start[1] == end[1]) && (start[2] == end[2]))
	{
		for (i = 0; i < w->numbrushes; i++)
		{
			brush_t *b = &w->b[i];

			if (!(b->contents & mask))
			{
				continue;
			}

			for (j = 0; j < b->numplanes; j++)
			{
				float ofs[3], dist;

				for (k = 0; k < 3; k++)
				{
					ofs[k] = (b->n[j][k] < 0) ? maxs[k] : mins[k];
				}

				dist = b->d[j] - Dot3(ofs, b->n[j]);

				if (Dot3(start, b->n[j]) - dist > 0)
				{
					break;
				}
			}

			if (j == b->numplanes)
			{
				tr->allsolid = tr->startsolid = 1;
				tr->fraction = 0;
				tr->contents = b->contents;
				lastent = b->ent;
				break;
			}
		}

		for (k = 0; k < 3; k++)
		{
			tr->endpos[k] = start[k];
		}

		tr->ent = &entnums[lastent];
		return;
	}

	for (i = 0; i < w->numbrushes && tr->fraction > 0; i++)
	{
		brush_t *b = &w->b[i];
		float enterfrac = -1, leavefrac = 1;
		int clip = -1, getout = 0, startout = 0;

		if (!(b->contents & mask))
		{
			continue;
		}

		for (j = 0; j < b->numplanes; j++)
		{
			float ofs[3], dist, d1, d2, f;

			for (k = 0; k < 3; k++)
			{
				ofs[k] = (b->n[j][k] < 0) ? maxs[k] : mins[k];
			}

			dist = b->d[j] - Dot3(ofs, b->n[j]);
			d1 = Dot3(start, b->n[j]) - dist;
			d2 = Dot3(end, b->n[j]) - dist;

			if (d2 > 0)
			{
				getout = 1;
			}

			if (d1 > 0)
			{
				startout = 1;
			}

			if ((d1 > 0) && (d2 >= d1))
			{
				goto nextbrush;
			}

			if ((d1 <= 0) && (d2 <= 0))
			{
				continue;
			}

			if (d1 > d2)
			{
				f = (d1 - 0.03125f) / (d1 - d2);

				if (f > enterfrac)
				{
					enterfrac = f;
					clip = j;
				}
			}
			else
			{
				f = (d1 + 0.03125f) / (d1 - d2);

				if (f < leavefrac)
				{
					leavefrac = f;
				}
			}
		}

		if (!startout)
		{
			tr->startsolid = 1;

			if (!getout)
			{
				tr->allsolid = 1;
			}

			tr->contents = b->contents;
			lastent = b->ent;
			continue;
		}

		if (enterfrac < leavefrac)
		{
			if ((enterfrac > -1) && (enterfrac < tr->fraction))
			{
				if (enterfrac < 0)
				{
					enterfrac = 0;
				}

				tr->fraction = enterfrac;

				for (k = 0; k < 3; k++)
				{
					tr->normal[k] = b->n[clip][k];
				}

				tr->surfflags = b->surfflags;
				tr->contents = b->contents;
				lastent = b->ent;
			}
		}
nextbrush:
		;
	}

	if (tr->fraction == 1)
	{
		for (k = 0; k < 3; k++)
		{
			tr->endpos[k] = end[k];
		}
	}
	else
	{
		for (k = 0; k < 3; k++)
		{
			tr->endpos[k] = start[k] + tr->fraction * (end[k] - start[k]);
		}
	}

	tr->ent = &entnums[lastent];
}

static int
W_PointContents(void *ctx, const float *p)
{
	world_t *w = ctx;
	int i, j, c = 0;

	for (i = 0; i < w->numbrushes; i++)
	{
		brush_t *b = &w->b[i];

		for (j = 0; j < b->numplanes; j++)
		{
			if (Dot3(p, b->n[j]) - b->d[j] >= 0)
			{
				break;
			}
		}

		if (j == b->numplanes)
		{
			c |= b->contents;
		}
	}

	return c;
}

static int
W_GroundIsClient(void *ctx, void *ent)
{
	world_t *w = ctx;
	int n = (int)((int *)ent - entnums);

	w->gic_calls++;
	w->gic_last = n;
	return (n >= 1) && (n <= w->nclients);
}

static void
W_DevPrint(void *ctx, const char *msg)
{
	world_t *w = ctx;

	if (strstr(msg, "EmergencySnapPosition"))
	{
		w->emergency++;
	}

	if (strstr(msg, "Bad InitialSnapPosition"))
	{
		w->badinitial++;
	}
}

static int EntNum(void *ent)
{
	return ent ? (int)((int *)ent - entnums) : -1;
}

/* ---- test state --------------------------------------------------------------- */

typedef struct
{
	world_t w;
	sofpm_t pm;
	sof_usercmd_t cmd;
	int moves;
} T;

static int failures, checks, verbose;
static const char *curname;

static void
Check(int ok, const char *fmt, ...)
{
	char buf[512];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	checks++;

	if (!ok)
	{
		failures++;
		printf("FAIL %s: %s\n", curname, buf);
	}
	else if (verbose)
	{
		printf("ok   %s: %s\n", curname, buf);
	}
}

static void
Init(T *t, const char *name)
{
	memset(t, 0, sizeof(*t));
	curname = name;
	t->pm.s.gravity = 800;
	t->pm.sizematters = 1;
	t->cmd.msec = 10;
	t->pm.trace = W_Trace;
	t->pm.pointcontents = W_PointContents;
	t->pm.groundisclient = W_GroundIsClient;
	t->pm.devprint = W_DevPrint;
	t->pm.ctx = &t->w;
	t->pm.mask = SOFPM_MASK_PLAYERSOLID;
}

static void
Standing(T *t)
{
	WorldF(&t->w);
	t->pm.s.origin[2] = 193;
	t->pm.s.pm_flags = 0x04;
}

static void
Org(T *t, int x, int y, int z)
{
	t->pm.s.origin[0] = (short)x;
	t->pm.s.origin[1] = (short)y;
	t->pm.s.origin[2] = (short)z;
}

static void
Vel(T *t, int x, int y, int z)
{
	t->pm.s.velocity[0] = (short)x;
	t->pm.s.velocity[1] = (short)y;
	t->pm.s.velocity[2] = (short)z;
}

/* run until t->moves == n */
static void
To(T *t, int n)
{
	while (t->moves < n)
	{
		if (t->moves > 0)
		{
			t->pm.snapinitial = 0;
		}

		t->pm.cmd = t->cmd;
		SoFPM_Move(&t->pm);
		t->moves++;
	}
}

#define S (t.pm.s)
#define CHK_O(X, Y, Z) Check(S.origin[0] == (X) && S.origin[1] == (Y) && S.origin[2] == (Z), \
		"x%d origin (%d %d %d) want (%d %d %d)", t.moves, S.origin[0], S.origin[1], S.origin[2], X, Y, Z)
#define CHK_V(X, Y, Z) Check(S.velocity[0] == (X) && S.velocity[1] == (Y) && S.velocity[2] == (Z), \
		"x%d velocity (%d %d %d) want (%d %d %d)", t.moves, S.velocity[0], S.velocity[1], S.velocity[2], X, Y, Z)
#define CHK_VX(X) Check(S.velocity[0] == (X), "x%d vx %d want %d", t.moves, S.velocity[0], X)
#define CHK_VZ(Z) Check(S.velocity[2] == (Z), "x%d vz %d want %d", t.moves, S.velocity[2], Z)
#define CHK_Z(Z) Check(S.origin[2] == (Z), "x%d z %d want %d", t.moves, S.origin[2], Z)
#define CHK_FL(F) Check(S.pm_flags == (F), "x%d flags 0x%x want 0x%x", t.moves, S.pm_flags, F)
#define CHK_TM(M) Check(S.pm_time == (M), "x%d timer %d want %d", t.moves, S.pm_time, M)
#define CHK_F(V, W, WHAT) Check(fabsf((V) - (W)) < 0.0015f, "x%d %s %.4f want %.4f", t.moves, WHAT, (double)(V), (double)(W))
#define CHK_I(V, W, WHAT) Check((V) == (W), "x%d %s %d want %d", t.moves, WHAT, (int)(V), (int)(W))

/* ---- vectors ------------------------------------------------------------------ */

static void
Ground(void)
{
	T t;

	Init(&t, "G1"); Standing(&t); t.cmd.forwardmove = 400;
	To(&t, 1); CHK_O(2, 0, 193); CHK_V(239, 0, 0);
	To(&t, 2); CHK_O(6, 0, 193); CHK_VX(431);
	To(&t, 3); CHK_O(12, 0, 193); CHK_VX(623);
	To(&t, 10); CHK_O(103, 0, 193); CHK_VX(1800);
	To(&t, 15); CHK_VX(2384);
	To(&t, 20); CHK_O(328, 0, 193); CHK_VX(2400);
	To(&t, 30); CHK_VX(2400);

	Init(&t, "G2"); Standing(&t); t.cmd.forwardmove = 200;
	To(&t, 1); CHK_VX(159);
	To(&t, 10); CHK_VX(1141);
	To(&t, 20); CHK_VX(1600);
	To(&t, 30); CHK_VX(1600);

	Init(&t, "G3"); Standing(&t); t.cmd.sidemove = 160;
	To(&t, 1); CHK_O(0, -1, 193); CHK_V(0, -127, 0);
	To(&t, 30); CHK_O(0, -274, 193); CHK_V(0, -1280, 0);

	Init(&t, "G4"); Standing(&t); t.cmd.forwardmove = 400; t.cmd.sidemove = 320;
	To(&t, 1); CHK_V(187, -149, 0);
	To(&t, 30); CHK_V(1874, -1498, 0);

	Init(&t, "G5"); Standing(&t); t.cmd.forwardmove = 400; t.cmd.msec = 50;
	To(&t, 1); CHK_O(60, 0, 193); CHK_VX(1200);
	To(&t, 2); CHK_O(162, 0, 193); CHK_VX(2040);
	To(&t, 3); CHK_VX(2400);

	Init(&t, "G6"); Standing(&t); t.cmd.forwardmove = 400; t.cmd.upmove = -200;
	To(&t, 1); CHK_FL(0x05); CHK_F(t.pm.maxs[2], 14, "maxs z"); CHK_F(t.pm.viewheight, 0, "viewheight"); CHK_VX(59);
	To(&t, 10); CHK_VX(167);
	To(&t, 40); CHK_VX(526);
	To(&t, 49); CHK_VX(600);
	To(&t, 60); CHK_VX(600);

	Init(&t, "G7"); Standing(&t); S.pm_flags = 0x05; Vel(&t, 2400, 0, 0); t.cmd.forwardmove = 400; t.cmd.upmove = -200;
	To(&t, 1); CHK_VX(2256);
	To(&t, 5); CHK_VX(1759);
	To(&t, 10); CHK_VX(1288);
	To(&t, 20); CHK_VX(685);
	To(&t, 23); CHK_VX(600);
	To(&t, 30); CHK_VX(600);

	Init(&t, "G8"); Standing(&t); Vel(&t, 2400, 0, 0);
	To(&t, 1); CHK_O(22, 0, 193); CHK_VX(2256);
	To(&t, 2); CHK_VX(2120);
	To(&t, 3); CHK_VX(1992);
	To(&t, 6); CHK_VX(1653);
	To(&t, 9); CHK_VX(1371);
	To(&t, 12); CHK_VX(1137);

	Init(&t, "G9"); Standing(&t); Vel(&t, 400, 0, 0);
	To(&t, 1); CHK_VX(352);
	To(&t, 3); CHK_VX(256);

	Init(&t, "G10"); Standing(&t); t.cmd.forwardmove = 400; t.pm.stagger = 0.25f;
	To(&t, 1); CHK_VX(179);

	Init(&t, "G11"); Standing(&t); t.cmd.forwardmove = 400; t.pm.stagger = 1.5f;
	To(&t, 3); CHK_O(0, 0, 193); CHK_V(0, 0, 0);

	Init(&t, "G12"); Standing(&t); t.cmd.forwardmove = 400; t.cmd.angles[0] = 10923;
	To(&t, 1); CHK_O(2, 0, 193); CHK_V(239, 0, 0);
	To(&t, 20); CHK_O(328, 0, 193);
	To(&t, 30); CHK_O(568, 0, 193); CHK_VX(2400);

	Init(&t, "G13"); Standing(&t); t.cmd.forwardmove = 400; t.cmd.angles[1] = 16384;
	To(&t, 1); CHK_O(0, 2, 193); CHK_V(0, 239, 0);

	Init(&t, "G14"); Standing(&t); Vel(&t, 0, 0, 720); t.cmd.forwardmove = 400;
	To(&t, 1); Check(S.pm_flags & 4, "on ground"); CHK_V(239, 0, 0);

	Init(&t, "G15"); Standing(&t); Vel(&t, 0, 0, 1200); t.cmd.forwardmove = 400;
	To(&t, 1); CHK_FL(0); CHK_O(0, 0, 204); CHK_V(24, 0, 1136);

	Init(&t, "G16"); Standing(&t); t.w.b[0].contents = 0x40001;
	To(&t, 1); CHK_VX(79);
	To(&t, 3); CHK_O(2, 0, 193); CHK_VX(142);

	Init(&t, "G17"); Standing(&t); t.w.b[0].surfflags = 2; Vel(&t, 2400, 0, 0);
	To(&t, 3); CHK_VX(2400); CHK_O(72, 0, 193);

	Init(&t, "G18"); Standing(&t); S.pm_type = 3; Vel(&t, 800, 0, 0); t.cmd.forwardmove = 400; t.cmd.upmove = 200;
	To(&t, 1); CHK_FL(0x05); CHK_VX(592);
	Check(!t.pm.cmd.forwardmove && !t.pm.cmd.sidemove && !t.pm.cmd.upmove, "command moves zeroed");
	To(&t, 2); CHK_VX(384);
	To(&t, 3); CHK_VX(176);

	Init(&t, "G19"); Standing(&t); S.pm_type = 6; Vel(&t, 800, 0, 0); t.cmd.forwardmove = 400; t.cmd.upmove = 200;
	To(&t, 1); CHK_O(0, 0, 193); CHK_V(800, 0, 0);
	Check(!t.pm.cmd.forwardmove && !t.pm.cmd.sidemove && !t.pm.cmd.upmove, "command moves zeroed");
}

static void
Jumping(void)
{
	T t;

	Init(&t, "J1"); Standing(&t); t.cmd.upmove = 200;
	To(&t, 1); CHK_FL(0x02); CHK_O(0, 0, 213); CHK_V(0, 0, 2096);
	Check(t.pm.groundentity == NULL, "no ground");
	Check(t.w.gic_calls == 1 && t.w.gic_last == 0, "callback calls %d last %d", t.w.gic_calls, t.w.gic_last);

	Init(&t, "J2"); Standing(&t); t.cmd.upmove = 200;
	To(&t, 33); CHK_Z(531); CHK_VZ(48);
	To(&t, 34); CHK_Z(530); CHK_VZ(-16);
	To(&t, 60); CHK_Z(289); CHK_VZ(-1680);

	Init(&t, "J3"); Standing(&t); t.cmd.upmove = 200; t.cmd.msec = 33;
	To(&t, 1); CHK_Z(257); CHK_VZ(1948);
	To(&t, 10); CHK_Z(518); CHK_VZ(40);
	To(&t, 19); CHK_Z(212); CHK_VZ(-1859);
	To(&t, 20); CHK_FL(0x16); CHK_TM(18); CHK_Z(193); CHK_VZ(0);
	To(&t, 21); CHK_TM(14);
	To(&t, 22); CHK_TM(10);
	To(&t, 23); CHK_TM(6);
	To(&t, 24); CHK_TM(2);
	To(&t, 25); CHK_FL(0x06);
	To(&t, 40); CHK_FL(0x06); CHK_Z(193);

	Init(&t, "J4"); Standing(&t); S.pm_flags = 0x06; t.cmd.upmove = 200;
	To(&t, 1); CHK_FL(0x06); CHK_VZ(0);

	Init(&t, "J5"); Standing(&t); S.pm_flags = 0x06;
	To(&t, 1); CHK_FL(0x04);

	Init(&t, "J6"); Standing(&t); S.pm_flags = 0x14; S.pm_time = 18; t.cmd.upmove = 200;
	To(&t, 1); CHK_FL(0x14); CHK_TM(17); CHK_VZ(0);

	Init(&t, "J7"); Standing(&t); t.cmd.upmove = 9;
	To(&t, 1); CHK_FL(0x04); CHK_VZ(0);

	Init(&t, "J8"); Standing(&t); Vel(&t, 0, 0, 400); t.cmd.upmove = 200;
	To(&t, 1); CHK_FL(0x02); CHK_O(0, 0, 213); CHK_V(0, 0, 2096);

	Init(&t, "J9"); Standing(&t); t.w.b[0].ent = 3; t.w.nclients = 8; t.cmd.upmove = 200;
	To(&t, 1); CHK_FL(0x04); CHK_V(0, 0, 0);
	Check(t.w.gic_calls == 1 && t.w.gic_last == 3, "callback calls %d last %d", t.w.gic_calls, t.w.gic_last);
	Check(t.pm.numtouch == 2 && EntNum(t.pm.touchents[0]) == 3 && EntNum(t.pm.touchents[1]) == 3,
			"touches %d", t.pm.numtouch);

	Init(&t, "J10"); Standing(&t); t.w.b[0].ent = 3; t.w.nclients = 2; t.cmd.upmove = 200;
	To(&t, 1); CHK_FL(0x02); CHK_O(0, 0, 213); CHK_V(0, 0, 2096);

	Init(&t, "L1"); WorldF(&t.w); Org(&t, 0, 0, 216); Vel(&t, 0, 0, -4000);
	To(&t, 1); CHK_FL(0x14); CHK_TM(25); CHK_O(0, 0, 193); CHK_V(0, 0, 0);
	Check(t.pm.numtouch == 3 && !EntNum(t.pm.touchents[0]) && !EntNum(t.pm.touchents[1]) &&
			!EntNum(t.pm.touchents[2]), "touches %d (0 0 0)", t.pm.numtouch);

	Init(&t, "L2"); WorldF(&t.w); Org(&t, 0, 0, 216); Vel(&t, 0, 0, -2400);
	To(&t, 1); CHK_FL(0x14); CHK_TM(18);

	Init(&t, "L3"); WorldF(&t.w); Org(&t, 0, 0, 216); Vel(&t, 0, 0, -1200);
	To(&t, 1); CHK_FL(0); CHK_O(0, 0, 203); CHK_VZ(-1264);

	Init(&t, "L4"); WorldF(&t.w); Org(&t, 0, 0, 216); Vel(&t, 0, 0, -4000); t.cmd.msec = 50;
	To(&t, 1); CHK_FL(0x04); CHK_VZ(0);

	Init(&t, "L5"); WorldF(&t.w); Org(&t, 0, 0, 200); Vel(&t, 0, 0, -4000); t.cmd.upmove = 200;
	To(&t, 1); CHK_FL(0x14); CHK_TM(25);
	To(&t, 2); CHK_FL(0x14); CHK_TM(24);
	To(&t, 3); CHK_FL(0x14); CHK_TM(23); CHK_VZ(0);

	Init(&t, "A1"); WorldF(&t.w); Org(&t, 0, 0, 800); t.cmd.forwardmove = 400;
	To(&t, 1); CHK_V(24, 0, -64); CHK_O(0, 0, 799);

	Init(&t, "A2"); WorldF(&t.w); Org(&t, 0, 0, 800); Vel(&t, 2400, 0, 0); t.cmd.forwardmove = 400; t.cmd.upmove = -200;
	To(&t, 1); CHK_FL(0x01); CHK_F(t.pm.maxs[2], 14, "maxs z"); CHK_F(t.pm.viewheight, 0, "viewheight");
	CHK_V(2400, 0, -64);

	Init(&t, "A3"); S.gravity = 0; S.pm_flags = 0x01; Org(&t, 0, 0, 800); t.cmd.forwardmove = 400; t.cmd.upmove = -200;
	To(&t, 30); CHK_V(720, 0, 0);

	Init(&t, "A4");
	To(&t, 10); CHK_O(0, 0, -30); CHK_V(0, 0, -640);
}

static void
DuckTimerAngles(void)
{
	T t;
	static const float sm[3] = {2, 1.3f, 0.1f}, st[3] = {39, 39, -8}, du[3] = {13, 7.2f, -10};
	int i;

	Init(&t, "D1"); Standing(&t); t.cmd.lean = 1; t.cmd.upmove = -200;
	To(&t, 1); CHK_FL(0x04); CHK_F(t.pm.maxs[2], 40, "maxs z");

	Init(&t, "D2"); Standing(&t); S.pm_flags = 0x05; t.cmd.lean = -1;
	To(&t, 1); Check(S.pm_flags & 1, "stays ducked");

	Init(&t, "D3"); Standing(&t); S.pm_flags = 0x05;
	To(&t, 1); CHK_FL(0x04); CHK_F(t.pm.maxs[2], 40, "maxs z"); CHK_F(t.pm.viewheight, 35, "viewheight");

	Init(&t, "D4"); Standing(&t); AddBox(&t.w, -8192, -8192, 50, 8192, 8192, 100, 1); S.pm_flags = 0x05;
	To(&t, 1); Check(S.pm_flags & 1, "stays ducked");

	Init(&t, "D5"); Standing(&t); t.pm.sizematters = 0.5f;
	To(&t, 1); CHK_F(t.pm.viewheight, 5.5f, "standing");
	Init(&t, "D5"); Standing(&t); t.pm.sizematters = 0.5f; S.pm_flags = 0x05; t.cmd.upmove = -200;
	To(&t, 1); CHK_F(t.pm.viewheight, -10, "ducked");

	for (i = 0; i < 3; i++)
	{
		Init(&t, "D6"); Standing(&t); t.pm.sizematters = sm[i];
		To(&t, 1); CHK_F(t.pm.viewheight, st[i], "standing");
		Init(&t, "D6"); Standing(&t); t.pm.sizematters = sm[i]; S.pm_flags = 0x05; t.cmd.upmove = -200;
		To(&t, 1); CHK_F(t.pm.viewheight, du[i], "ducked");
	}

	Init(&t, "T1"); Standing(&t); S.pm_flags = 0x14; S.pm_time = 18; t.cmd.msec = 50;
	To(&t, 1); CHK_TM(12);

	Init(&t, "T2"); Standing(&t); S.pm_flags = 0x14; S.pm_time = 18; t.cmd.msec = 5;
	To(&t, 1); CHK_TM(17);

	Init(&t, "T3"); Standing(&t); S.pm_flags = 0x14; S.pm_time = 2; t.cmd.msec = 30;
	To(&t, 1); CHK_FL(0x04); CHK_TM(0);

	Init(&t, "T4"); Standing(&t); S.pm_flags = 0x34; S.pm_time = 10; Vel(&t, 2400, 0, 0);
	t.cmd.angles[0] = 2000; t.cmd.angles[1] = 3000; t.cmd.angles[2] = 500; t.cmd.forwardmove = 400;
	To(&t, 1); CHK_O(0, 0, 193); CHK_VX(2400); CHK_TM(9);
	CHK_F(t.pm.viewangles[0], 0, "pitch"); CHK_F(t.pm.viewangles[1], 16.479f, "yaw"); CHK_F(t.pm.viewangles[2], 0, "roll");

	Init(&t, "V1"); Standing(&t); t.cmd.angles[0] = 21845;
	To(&t, 1); CHK_F(t.pm.viewangles[0], 89, "pitch");

	Init(&t, "V2"); Standing(&t); t.cmd.angles[0] = -21845;
	To(&t, 1); CHK_F(t.pm.viewangles[0], -119.998f, "pitch");

	Init(&t, "V3"); Standing(&t); t.cmd.angles[0] = -500; t.cmd.angles[1] = 30000;
	S.delta_angles[0] = 1000; S.delta_angles[1] = -2000;
	To(&t, 1); CHK_F(t.pm.viewangles[0], 2.747f, "pitch"); CHK_F(t.pm.viewangles[1], 153.809f, "yaw");
	CHK_F(t.pm.viewangles[2], 0, "roll");
}

static void
SnapTypes(void)
{
	T t;
	int ty;

	/*
	 * SPEC-QUESTION: S1 says world E (gravity 800) ends at (1, 1, 193). The snap
	 * to 193 happens, but the same move then falls 0.08 units (vz -64/8) and the
	 * final snap truncates to 192, consistent with A4. Checked here: the snap
	 * itself (gravity 0) and the fall that follows it (gravity 800).
	 */
	Init(&t, "S1 gravity 0"); S.gravity = 0; Org(&t, 0, 0, 192); t.pm.snapinitial = 1;
	To(&t, 1); CHK_O(1, 1, 193);
	Init(&t, "S1 gravity 800"); Org(&t, 0, 0, 192); t.pm.snapinitial = 1;
	To(&t, 1); CHK_O(1, 1, 192); CHK_VZ(-64);
	printf("NOTE S1: spec says (1, 1, 193) with gravity 800; this routine gives (%d, %d, %d) after the 0.08 fall "
			"(see SPEC-QUESTION in the source)\n", S.origin[0], S.origin[1], S.origin[2]);

	Init(&t, "S2"); WorldF(&t.w); Org(&t, 0, 0, 192); t.pm.snapinitial = 1;
	To(&t, 1); CHK_O(1, 1, 193);

	Init(&t, "S3"); WorldF(&t.w); Org(&t, 0, 0, 192);
	To(&t, 1); CHK_O(0, 0, 192);

	Init(&t, "S4"); AddBox(&t.w, -8192, -8192, -64, 8192, 8192, 1, 1); Org(&t, 0, 0, 192); t.cmd.forwardmove = 400;
	To(&t, 1); CHK_O(10, 10, 202); Check(S.pm_flags & 4, "on ground"); CHK_VX(239);
	Check(t.w.emergency == 1, "EmergencySnapPosition %d", t.w.emergency);

	Init(&t, "S5"); WorldF(&t.w); Org(&t, 0, 0, 192); t.cmd.forwardmove = 400;
	To(&t, 1); CHK_O(10, 10, 202); Check(!(S.pm_flags & 4), "not on ground");

	Init(&t, "S6"); AddBox(&t.w, -8192, -8192, -8192, 8192, 8192, 8192, 1); Org(&t, 0, 0, 192);
	t.pm.snapinitial = 1; t.cmd.forwardmove = 400;
	To(&t, 1); CHK_O(0, 0, 192); CHK_VX(239);
	Check(t.w.emergency == 1 && t.w.badinitial == 1, "messages emergency %d bad initial %d", t.w.emergency,
			t.w.badinitial);

	Init(&t, "Y1"); S.pm_type = 1; t.cmd.forwardmove = 400;
	To(&t, 1); CHK_O(2, 0, 0); CHK_VX(239); CHK_F(t.pm.viewheight, 35, "viewheight");
	To(&t, 2); CHK_O(6, 0, 0); CHK_VX(407);

	Init(&t, "Y2"); S.pm_type = 1; t.cmd.angles[0] = -5461; t.cmd.forwardmove = 400; t.cmd.upmove = 100;
	To(&t, 1); CHK_O(1, 0, 1); CHK_V(181, 0, 157);

	Init(&t, "Y3"); S.pm_type = 1; Vel(&t, 2400, 0, 0);
	To(&t, 1); CHK_VX(2184); CHK_O(0, 0, 0);
	To(&t, 2); CHK_VX(1987); CHK_O(0, 0, 0);

	Init(&t, "Y4"); WorldF(&t.w); S.pm_type = 4; Org(&t, 0, 0, 8);
	To(&t, 1); CHK_F(t.pm.mins[2], 0, "mins z"); CHK_F(t.pm.maxs[2], 16, "maxs z"); CHK_F(t.pm.mins[0], -16, "mins x");
	CHK_F(t.pm.viewheight, 8, "viewheight"); CHK_O(0, 0, 7); CHK_VZ(-64);

	for (ty = 5; ty <= 7; ty += 2)
	{
		Init(&t, ty == 5 ? "Y5 type 5" : "Y5 type 7"); Standing(&t); S.pm_type = ty; Vel(&t, 800, 0, 0);
		t.cmd.forwardmove = 400; t.cmd.upmove = 200;
		To(&t, 1); CHK_O(0, 0, 193); CHK_V(800, 0, 0); CHK_FL(0x04); CHK_F(t.pm.viewheight, 0, "viewheight");
		CHK_I(t.pm.numtouch, 0, "touches");
		Check(!t.pm.cmd.forwardmove && !t.pm.cmd.sidemove && !t.pm.cmd.upmove, "command moves zeroed");
	}

	Init(&t, "Y6"); WorldF(&t.w); S.pm_type = 6; Org(&t, 0, 0, 800); Vel(&t, 800, 0, -800);
	To(&t, 3); CHK_O(0, 0, 800); CHK_V(800, 0, -800);
}

static void
Steps(void)
{
	T t;
	brush_t *b;

	Init(&t, "P1"); Standing(&t); AddBox(&t.w, 40, -512, 0, 512, 512, 16, 1); t.cmd.forwardmove = 400; Vel(&t, 2400, 0, 0);
	To(&t, 7); CHK_O(168, 0, 193);
	To(&t, 8); CHK_O(192, 0, 321); CHK_VX(2400);
	Init(&t, "P1 msec 50"); Standing(&t); AddBox(&t.w, 40, -512, 0, 512, 512, 16, 1); t.cmd.forwardmove = 400;
	Vel(&t, 2400, 0, 0); t.cmd.msec = 50;
	To(&t, 2); CHK_O(240, 0, 321);

	Init(&t, "P2"); Standing(&t); AddBox(&t.w, 40, -512, 0, 512, 512, 20, 1); t.cmd.forwardmove = 400; Vel(&t, 2400, 0, 0);
	To(&t, 10); CHK_O(191, 0, 193); CHK_V(0, 0, 0);

	Init(&t, "P3"); Standing(&t); AddBox(&t.w, 40, -512, 0, 512, 512, 16, 1); t.cmd.forwardmove = 400;
	Org(&t, 480, 0, 321); t.cmd.angles[1] = (short)32768; Vel(&t, -2400, 0, 0);
	To(&t, 10); CHK_O(240, 0, 321);
	To(&t, 13); CHK_O(168, 0, 321); Check(!(S.pm_flags & 4), "airborne");
	To(&t, 14); CHK_O(144, 0, 320); CHK_VZ(-64);
	To(&t, 20); CHK_O(0, 0, 300);
	To(&t, 25); CHK_O(-120, 0, 265);

	Init(&t, "P4"); S.gravity = 0; S.pm_flags = 0x04; b = NewBrush(&t.w, 1, 0);
	AddPlane(b, -0.70710678f, 0.70710678f, 0, -28.2842712f);
	Org(&t, 52, 0, 0); Vel(&t, 2400, 0, 0);
	To(&t, 1); CHK_O(69, 6, 0); CHK_V(1188, 1212, 0); /* Q2 clip, no SoF rub */

	Init(&t, "P5"); S.gravity = 0; S.pm_flags = 0x04;
	AddBox(&t.w, 20, -512, -512, 100, 512, 512, 1); AddBox(&t.w, -512, 20, -512, 512, 100, 512, 1);
	Org(&t, 16, 16, 0); Vel(&t, 2400, 2400, 0);
	To(&t, 1); CHK_O(31, 31, 0); CHK_V(0, 0, 0); CHK_I(t.pm.numtouch, 6, "touches");

	Init(&t, "P6"); Standing(&t); b = NewBrush(&t.w, 1, 0);
	AddPlane(b, -0.5f, 0, 0.8660254f, -50); AddPlane(b, 1, 0, 0, 2000); AddPlane(b, 0, 0, -1, 0);
	Org(&t, 400, 0, 193); t.cmd.forwardmove = 400; Vel(&t, 2400, 0, 0);
	To(&t, 11); CHK_O(664, 0, 193);
	To(&t, 12); CHK_O(688, 0, 202);
	To(&t, 20); CHK_O(880, 0, 313); CHK_V(2400, 0, 0);
}

static void
WaterLadders(void)
{
	T t;
	static const int k1cycle[8] = {1615, 1615, 1623, 1621, 1620, 1619, 1618, 1617};
	int i;

#define WATER(z) AddBox(&t.w, -8192, -8192, 0, 8192, 8192, z, 0x20)
#define ALLWATER(c) AddBox(&t.w, -8192, -8192, -8192, 8192, 8192, 8192, c)
#define LEVEL(L) CHK_I(t.pm.waterlevel, L, "water level")

	Init(&t, "W1"); Standing(&t); WATER(20); To(&t, 1); LEVEL(1); CHK_I(t.pm.watertype, 0x20, "type");
	Init(&t, "W1d"); Standing(&t); WATER(20); S.pm_flags = 0x05; t.cmd.upmove = -200; To(&t, 1); LEVEL(2);
	Init(&t, "W2"); Standing(&t); WATER(30); To(&t, 1); LEVEL(2);
	Init(&t, "W2d"); Standing(&t); WATER(30); S.pm_flags = 0x05; t.cmd.upmove = -200; To(&t, 1); LEVEL(3);
	Init(&t, "W3"); Standing(&t); WATER(60); To(&t, 1); LEVEL(3);
	Init(&t, "W3d"); Standing(&t); WATER(60); S.pm_flags = 0x05; t.cmd.upmove = -200; To(&t, 1); LEVEL(3);

	Init(&t, "W4"); Standing(&t); WATER(30); t.cmd.forwardmove = 400;
	To(&t, 1); CHK_VX(119);
	To(&t, 30); CHK_VX(1200);

	Init(&t, "W5"); Standing(&t); WATER(20); t.cmd.forwardmove = 400;
	To(&t, 30); CHK_VX(2400);

	Init(&t, "W6 0.5"); Standing(&t); WATER(30); t.pm.sizematters = 0.5f; To(&t, 1); LEVEL(3);
	Init(&t, "W6 2"); Standing(&t); WATER(30); t.pm.sizematters = 2; To(&t, 1); LEVEL(1);

	Init(&t, "W7"); ALLWATER(0x20);
	To(&t, 1); CHK_V(0, 0, -23);
	To(&t, 5); CHK_VZ(-110); CHK_O(0, 0, -1);

	Init(&t, "W8"); ALLWATER(0x20); t.cmd.forwardmove = 400;
	To(&t, 1); CHK_VX(119);
	To(&t, 30); CHK_VX(1200);

	Init(&t, "W9"); ALLWATER(0x20); t.cmd.upmove = 200;
	To(&t, 1); CHK_VZ(800); CHK_Z(8);
	To(&t, 2); CHK_VZ(800); CHK_Z(16);

	Init(&t, "W10"); ALLWATER(0x20); Vel(&t, 0, 0, -2400); t.cmd.upmove = 200;
	To(&t, 1); CHK_VZ(-2248);

	Init(&t, "W11 slime"); ALLWATER(0x10); t.cmd.upmove = 200; To(&t, 1); CHK_VZ(700);
	Init(&t, "W11 lava"); ALLWATER(0x08); t.cmd.upmove = 200; To(&t, 1); CHK_VZ(468);

	Init(&t, "W12 up"); ALLWATER(0x60020); t.cmd.upmove = 200; To(&t, 1); CHK_V(107, 0, 441);
	Init(&t, "W12"); ALLWATER(0x60020);
	To(&t, 1); CHK_V(118, 0, -17);
	To(&t, 3); CHK_V(344, 0, -50);

	Init(&t, "W13"); ALLWATER(0x20); t.cmd.forwardmove = 400; t.pm.stagger = 0.5f;
	To(&t, 1); CHK_VX(59);

	Init(&t, "W14"); Standing(&t); WATER(40); AddBox(&t.w, 30, -512, 0, 512, 512, 32, 1);
	Org(&t, 16, 0, 193); t.cmd.forwardmove = 400; t.cmd.angles[0] = -5461;
	To(&t, 1); CHK_FL(0x08); CHK_TM(254); CHK_O(29, 0, 220); CHK_V(1385, 0, 2736);
	To(&t, 10); CHK_TM(245); CHK_Z(433); LEVEL(1);
	To(&t, 40); CHK_O(488, 0, 769); CHK_VZ(240); Check(S.pm_flags & 8, "still water jump");
	Init(&t, "W14 pitch 0"); Standing(&t); WATER(40); AddBox(&t.w, 30, -512, 0, 512, 512, 32, 1);
	Org(&t, 16, 0, 193); t.cmd.forwardmove = 400;
	To(&t, 1); CHK_V(1600, 0, 2736);

	Init(&t, "W15"); S.pm_flags = 0x08; S.pm_time = 255; Vel(&t, 800, 0, 2800);
	To(&t, 1); CHK_TM(254); CHK_V(800, 0, 2736);
	Init(&t, "W15b"); S.pm_flags = 0x08; S.pm_time = 255; Vel(&t, 800, 0, 40);
	To(&t, 1); CHK_FL(0); CHK_TM(0); CHK_V(800, 0, -24);

#define LADDER(h) AddBox(&t.w, 17, -512, 0, 40, 512, h, 0x20000001)
	Init(&t, "K1"); Standing(&t); LADDER(512); t.cmd.angles[0] = -3641; t.cmd.forwardmove = 200;
	To(&t, 1); CHK_O(0, 0, 194); CHK_V(20, 0, 160); CHK_FL(0);
	To(&t, 3); CHK_V(48, 0, 384);
	To(&t, 10); CHK_O(4, 0, 254); CHK_V(141, 0, 1142);
	Init(&t, "K1 2048"); Standing(&t); LADDER(2048); t.cmd.angles[0] = -3641; t.cmd.forwardmove = 200;

	for (i = 0; i < 8; i++)
	{
		To(&t, 20 + i); CHK_VZ(k1cycle[i]);
	}

	Init(&t, "K2"); WorldF(&t.w); LADDER(512); Org(&t, 0, 0, 800); t.cmd.angles[0] = 3641; t.cmd.forwardmove = 200;
	To(&t, 1); CHK_V(20, 0, -160);

	Init(&t, "K3"); WorldF(&t.w); LADDER(512); Org(&t, 0, 0, 800); t.cmd.upmove = 200;
	To(&t, 1); CHK_V(0, 0, 159);
	To(&t, 3); CHK_VZ(383);

	Init(&t, "K4"); WorldF(&t.w); LADDER(512); Org(&t, 0, 0, 800); Vel(&t, 0, 0, 800);
	To(&t, 1); CHK_VZ(688);
	To(&t, 3); CHK_VZ(464);

	Init(&t, "K5"); WorldF(&t.w); LADDER(512); Org(&t, 0, 0, 800); Vel(&t, 0, 0, -2400); t.cmd.upmove = 200;
	To(&t, 1); CHK_VZ(-2192);
}

/* section 19/20 command expansion */
/* OpenMerc regressions of the Q2 rebase (docs/DEVIATIONS.md) */
static void
Hover(void)
{
	T t;
	int i, grounded = 0, firstground = -1;

	/* kos3 (-863 16 -28) trace: feet floor at -88, rest -64.0; the player at
	   -509 (0.375 u up), airborne, sliding (13 66) u/s, 6 ms moves (165 fps).
	   1.07f + truncating snap: ground only at the end of every 4th move for
	   ever, no friction - the "stuck, sliding, cannot jump" bug. */
	Init(&t, "H1 hover"); AddBox(&t.w, -8192, -8192, -200, 8192, 8192, -88, 1);
	t.cmd.msec = 6; Org(&t, 0, 0, -509); Vel(&t, 107, 531, 0);
	for (i = 1; i <= 60; i++)
	{
		To(&t, i);
		if (S.pm_flags & 0x04)
		{
			if (firstground < 0)
			{
				firstground = i;
			}
			grounded++;
		}
	}
	Check(firstground > 0 && firstground <= 6, "first ground at move %d", firstground);
	Check(grounded == 61 - firstground, "on ground %d of %d moves after landing", grounded, 61 - firstground);
	Check(S.origin[2] <= -510 && S.origin[2] >= -512, "rest z %d", S.origin[2]);
	To(&t, 200);
	CHK_V(0, 0, 0);
	t.cmd.upmove = 200; To(&t, 201);
	Check(S.velocity[2] > 2000, "jump vz %d", S.velocity[2]);

	/* same at 8 ms and 4 ms moves (125 / 250 fps) */
	Init(&t, "H2 hover 8 ms"); AddBox(&t.w, -8192, -8192, -200, 8192, 8192, -88, 1);
	t.cmd.msec = 8; Org(&t, 0, 0, -509); Vel(&t, 107, 531, 0);
	To(&t, 150); CHK_V(0, 0, 0); CHK_FL(0x04);
	Init(&t, "H3 hover 4 ms"); AddBox(&t.w, -8192, -8192, -200, 8192, 8192, -88, 1);
	t.cmd.msec = 4; Org(&t, 0, 0, -509); Vel(&t, 107, 531, 0);
	To(&t, 300); CHK_V(0, 0, 0); CHK_FL(0x04);

	/* snap on ground keeps the floor-side value: 0.25 u up stays on ground */
	Init(&t, "H4 ground snap"); AddBox(&t.w, -8192, -8192, -200, 8192, 8192, -88, 1);
	t.cmd.msec = 6; Org(&t, 0, 0, -510); S.pm_flags = 0x04; Vel(&t, 0, 800, 0);
	for (i = 1, grounded = 0; i <= 40; i++)
	{
		To(&t, i);
		grounded += (S.pm_flags & 0x04) && (S.origin[2] <= -510);
	}
	CHK_I(grounded, 40, "grounded moves");

	/* too steep a slope (n.z 0.6): Q2 slides down, 1.07f zeroed vz each move */
	Init(&t, "H5 steep"); t.cmd.msec = 10;
	AddPlane(NewBrush(&t.w, 1, 0), -0.8f, 0, 0.6f, 0);
	Org(&t, 0, 0, 400); Vel(&t, 0, 0, 0);
	To(&t, 60);
	Check(S.velocity[2] < -400, "steep slope vz %d", S.velocity[2]);
	Check(!(S.pm_flags & 0x04), "steep slope airborne");

	/* kos2 sewer pipe (docs/KNOWN_ISSUES.md "kos2 sewer pipe drift"): a box
	   wedged in the crease of a walkable facet (n.z 0.857) and a too-steep one
	   (n.z 0.514) is held up by both; a full-box ground probe that returned the
	   steep plane would leave the player airborne without friction and the crease
	   would keep his velocity along the pipe. The in-game drift is gone with the Q2 rebase (X1 kos2_pipe, 60/165/300 fps); this pins it. */
	for (i = 0; i < 3; i++)
	{
		static const char *names[3] = { "H6 pipe crease 17 ms", "H7 pipe crease 6 ms", "H8 pipe crease 3 ms" };
		static const int ms[3] = { 17, 6, 3 };
		int n, y0, still;

		Init(&t, names[i]); t.cmd.msec = ms[i];
		/* steep first: the test trace keeps the first of equal hits */
		AddPlane(NewBrush(&t.w, 1, 0), 0.857f, 0, 0.514f, 0);
		AddPlane(NewBrush(&t.w, 1, 0), -0.514f, 0, 0.857f, 0);		Org(&t, 8 * 8, 0, 60 * 8); Vel(&t, 0, 400 * 8, 0);
		n = 2000 / ms[i];
		To(&t, n / 4);
		y0 = S.origin[1];
		To(&t, n);
		still = S.origin[1];
		To(&t, n + n / 4);
		Check(S.origin[1] == still, "still gliding along the pipe: y %d -> %d (vy %d, n.z %.3f)",
				still / 8, S.origin[1] / 8, S.velocity[1] / 8, (double)t.pm.probenormal[2]);
		Check(S.pm_flags & 0x04, "on ground in the crease (flags 0x%x, n.z %.3f)", S.pm_flags, (double)t.pm.probenormal[2]);
		Check(S.velocity[1] == 0, "vy %d", S.velocity[1]);
		(void)y0;
	}
}

static void
Expansion(void)
{
	T t;
	sof_usercmd_t c;
	int n;

	Init(&t, "X1 run doubles"); Standing(&t);
	memset(&c, 0, sizeof(c)); c.msec = 10; c.forwardmove = 200; c.buttons = 0x20;
	n = SoFPM_RunCommand(&t.pm, &c, 0x04, 255);
	CHK_I(n, 1, "moves"); CHK_I(t.pm.cmd.forwardmove, 400, "forward"); CHK_VX(239);

	Init(&t, "X2 exhausted"); Standing(&t);
	SoFPM_RunCommand(&t.pm, &c, 0x40, 255);
	CHK_I(t.pm.cmd.forwardmove, 200, "forward"); CHK_VX(159);

	Init(&t, "X3 scale"); Standing(&t);
	c.buttons = 0; c.sidemove = 160; c.upmove = 0;
	SoFPM_RunCommand(&t.pm, &c, 0, 128);
	CHK_I(t.pm.cmd.forwardmove, 100, "forward"); CHK_I(t.pm.cmd.sidemove, 80, "side");

	Init(&t, "X4 split"); Standing(&t); Vel(&t, 0, 0, -8);
	memset(&c, 0, sizeof(c)); c.msec = 47;
	n = SoFPM_RunCommand(&t.pm, &c, 0, 255);
	CHK_I(n, 4, "moves"); CHK_I(t.pm.cmd.msec, 10, "last msec");

	Init(&t, "X5 no split on ground"); Standing(&t);
	n = SoFPM_RunCommand(&t.pm, &c, 0, 255);
	CHK_I(n, 1, "moves"); CHK_I(t.pm.cmd.msec, 47, "msec");
}

int
main(int argc, char **argv)
{
	int i;

	for (i = 0; i < 64; i++)
	{
		entnums[i] = i;
	}

	verbose = (argc > 1) && !strcmp(argv[1], "-v");

	Ground();
	Jumping();
	DuckTimerAngles();
	SnapTypes();
	Steps();
	WaterLadders();
	Hover();
	Expansion();

	printf("sofpm_test: %d checks, %d failed\n", checks, failures);
	return failures;
}
