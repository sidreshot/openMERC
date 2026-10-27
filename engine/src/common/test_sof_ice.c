/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * sofpm_ice: measurement harness for the "ice skating" report (owner,
 * 2026-10-27): sharp WASD direction changes and strafing with always-run
 * keep sliding along the old direction. Runs SoFPM_RunCommand on the flat
 * synthetic floor of the SPEC-314 vectors (world F) the way the client
 * prediction / server do (run button, q 255) and prints:
 *   snap90  W held 1 s, then D only: old-axis speed 25/50/100/150 ms after
 *           the snap, time until |old axis| < 30, skid distance along the
 *           old axis, time until the new axis reaches 270
 *   diag    W+D steady, then W+A (90 degree wish change between diagonals):
 *           velocity orthogonal to the new wish at 50/100 ms, settle time
 *   rapid   120 s of key changes every 120 ms (90 degree jumps), stop:
 *           mean/max |v orthogonal to wish|, max speed, airborne ticks,
 *           speed 1 s after releasing every key
 * Usage: sofpm_ice [msec ...]  (default 8 16 33). Exit code 0, or 1 when
 * the flat-floor walker ever leaves the ground, does not stop, or exceeds
 * 345 u/s. Quake II / SoF 1.07f acceleration only limits the speed along
 * the wish, so chained 90 degree changes reach ~330 u/s (SPEC-314, stock;
 * the OpenMerc LateralFriction that hid it was dropped with the Q2 rebase -
 * the in-game "ice" was the hover above the floor, see sofpm_test H1-H4).
 *
 * Built from test_sof_pmove.c's synthetic world (its main is renamed).
 */

#define main sofpm_vectors_main
#include "test_sof_pmove.c"
#undef main

#define RUN 0x20

typedef struct
{
	T t;
	int msec;
	double time;
	double pos[2];
	int airticks;
	double maxspeed;
} sim_t;

static void
SimInit(sim_t *s, int msec)
{
	memset(s, 0, sizeof(*s));
	Init(&s->t, "ice");
	Standing(&s->t);
	s->msec = msec;
}

static void
VelF(const sim_t *s, double *v)
{
	v[0] = s->t.pm.s.velocity[0] * 0.125;
	v[1] = s->t.pm.s.velocity[1] * 0.125;
}

/* one command with forward f and side r in {-1,0,1} */
static void
Tick(sim_t *s, int f, int r)
{
	sof_usercmd_t c;
	double v[2], sp;

	memset(&c, 0, sizeof(c));
	c.msec = (uint8_t)s->msec;
	c.buttons = RUN;
	c.forwardmove = (int16_t)(200 * f);
	c.sidemove = (int16_t)(200 * r);
	SoFPM_RunCommand(&s->t.pm, &c, s->t.pm.s.pm_flags, 255);

	/* keep the walker near the origin (1/8 unit shorts) */
	if (abs(s->t.pm.s.origin[0]) > 16000 || abs(s->t.pm.s.origin[1]) > 16000)
	{
		s->t.pm.s.origin[0] = 0;
		s->t.pm.s.origin[1] = 0;
	}

	VelF(s, v);
	sp = sqrt(v[0] * v[0] + v[1] * v[1]);

	if (sp > s->maxspeed)
	{
		s->maxspeed = sp;
	}

	if (!(s->t.pm.s.pm_flags & SOFPMF_ON_GROUND))
	{
		s->airticks++;
	}

	s->pos[0] += v[0] * s->msec * 0.001;
	s->pos[1] += v[1] * s->msec * 0.001;
	s->time += s->msec * 0.001;
}

static void
Hold(sim_t *s, int f, int r, double seconds)
{
	int n = (int)(seconds * 1000 / s->msec + 0.5), i;

	for (i = 0; i < n; i++)
	{
		Tick(s, f, r);
	}
}

/* forward = +x, right = -y at yaw 0 */
static void
WishDir(int f, int r, double *w)
{
	double l;

	w[0] = f;
	w[1] = -r;
	l = sqrt(w[0] * w[0] + w[1] * w[1]);

	if (l > 0)
	{
		w[0] /= l;
		w[1] /= l;
	}
}

static double
Perp(const sim_t *s, int f, int r)
{
	double v[2], w[2], d;

	VelF(s, v);
	WishDir(f, r, w);
	d = v[0] * w[0] + v[1] * w[1];
	return sqrt((v[0] - w[0] * d) * (v[0] - w[0] * d) + (v[1] - w[1] * d) * (v[1] - w[1] * d));
}

static int bad;

static void
Snap90(int msec)
{
	sim_t s;
	double v[2], x0, t0, at[4] = {0.025, 0.05, 0.1, 0.15}, val[4] = {0};
	double tstop = -1, tnew = -1;
	int k = 0;

	SimInit(&s, msec);
	Hold(&s, 1, 0, 1.0);
	VelF(&s, v);
	x0 = s.pos[0];
	t0 = s.time;
	printf("  snap90  msec %2d: steady %.1f u/s;", msec, v[0]);

	while (s.time - t0 < 1.0)
	{
		Tick(&s, 0, 1);
		VelF(&s, v);

		while (k < 4 && s.time - t0 >= at[k] - 1e-9)
		{
			val[k++] = v[0];
		}

		if (tstop < 0 && fabs(v[0]) < 30)
		{
			tstop = s.time - t0;
		}

		if (tnew < 0 && -v[1] >= 270)
		{
			tnew = s.time - t0;
		}
	}

	printf(" old axis @25/50/100/150 ms %.0f/%.0f/%.0f/%.0f; |old|<30 after %.0f ms;"
			" skid %.1f u; new axis 270 after %.0f ms\n",
			val[0], val[1], val[2], val[3], tstop * 1000, s.pos[0] - x0, tnew * 1000);
	bad |= s.airticks != 0 || s.maxspeed > 345;
}

static void
Diag(int msec)
{
	sim_t s;
	double t0, p50 = 0, p100 = 0, tset = -1, p;
	int i50 = 0, i100 = 0;

	SimInit(&s, msec);
	Hold(&s, 1, 1, 1.0);
	t0 = s.time;

	while (s.time - t0 < 1.0)
	{
		Tick(&s, 1, -1);
		p = Perp(&s, 1, -1);

		if (!i50 && s.time - t0 >= 0.05 - 1e-9)
		{
			p50 = p;
			i50 = 1;
		}

		if (!i100 && s.time - t0 >= 0.1 - 1e-9)
		{
			p100 = p;
			i100 = 1;
		}

		if (tset < 0 && p < 30)
		{
			tset = s.time - t0;
		}
	}

	printf("  diag    msec %2d: WD->WA orthogonal @50/100 ms %.0f/%.0f; <30 after %.0f ms\n",
			msec, p50, p100, tset * 1000);
	bad |= s.airticks != 0 || s.maxspeed > 345;
}

static void
Rapid(int msec)
{
	/* 90 degree jumps: W, D, S, A, WD, SD, SA, WA in a scrambled cycle */
	static const int keys[][2] = {
		{1, 0}, {0, 1}, {1, 0}, {0, -1}, {-1, 0}, {0, 1}, {1, 1}, {1, -1},
		{-1, -1}, {1, -1}, {0, -1}, {1, 0}, {-1, 1}, {1, 1}, {0, 1}, {-1, 0}
	};
	sim_t s;
	double sum = 0, mx = 0, p, v[2], sp;
	long n = 0;
	int seg = 0, i;
	SimInit(&s, msec);

	while (s.time < 120.0)
	{
		const int *k = keys[seg++ % 16];
		int per = (int)(120.0 / msec + 0.5);

		for (i = 0; i < per; i++)
		{
			Tick(&s, k[0], k[1]);
			p = Perp(&s, k[0], k[1]);
			sum += p;
			n++;

			if (p > mx)
			{
				mx = p;
			}
		}
	}

	Hold(&s, 0, 0, 1.0);
	VelF(&s, v);
	sp = sqrt(v[0] * v[0] + v[1] * v[1]);
	printf("  rapid   msec %2d: 120 s, mean |v orth wish| %.1f, max %.0f, max speed %.1f,"
			" airborne ticks %d, speed 1 s after release %.1f\n",
			msec, sum / n, mx, s.maxspeed, s.airticks, sp);
	bad |= s.airticks != 0 || s.maxspeed > 345 || sp != 0;
}

int
main(int argc, char **argv)
{
	int ms[8] = {8, 16, 33}, nm = 3, i;

	if (argc > 1)
	{
		nm = 0;

		for (i = 1; i < argc && nm < 8; i++)
		{
			ms[nm++] = atoi(argv[i]);
		}
	}

	printf("sofpm_ice (always-run, flat floor, q 255)\n");

	for (i = 0; i < nm; i++)
	{
		Snap90(ms[i]);
		Diag(ms[i]);
		Rapid(ms[i]);
	}

	printf("sofpm_ice: %s\n", bad ? "FLAGGED (airborne, no stop or >345 u/s)" : "ok");
	return bad;
}
