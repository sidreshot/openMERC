/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2026 OpenMerc contributors
 *
 * The SoF 1.07f player-movement routine, written from SPEC-314. It
 * descends from Quake II 3.19 pmove.c; every SoF rule (box, duck, view
 * heights, step-slide, snaps, stagger, ladder and
 * water-jump details) follows the spec. One routine for the server's game
 * import #81 (sv_pmove.c) and the client's prediction (cl_prediction.c).
 *
 * Arithmetic: IEEE single precision with rounding after every operation
 * (SPEC-314 section 1). All literals are float; the only double
 * operations are the angle-to-radian product and sin/cos, rounded to
 * float at once, as the original's x87 in 24-bit precision does.
 *
 * Implements: SPEC-314 Behaviour 1-17, sections 19-20 (command expansion)
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "header/sof_pmove.h"

#define STEPSIZE 18.0f
#define MIN_STEP_NORMAL 0.7f
#define STOP_EPSILON 0.1f
#define MAX_CLIP_PLANES 5
#define OVERBOUNCE 1.01f

#define CONTENTS_SOLID 0x1
#define MASK_WATER 0x38
#define MASK_CURRENT 0xFC0000
#define CONTENTS_LADDER 0x20000000
#define SURF_SLICK 0x2

#define ANGLE_UNIT 0.005493164f /* 360 / 65536 */

typedef struct
{
	sofpm_t *pm;

	float origin[3];
	float velocity[3];
	float forward[3], right[3], up[3];
	float frametime;
	float k;                  /* stagger acceleration scale */

	short fallback[3];        /* stored origin to fall back to */

	int groundsurfflags;
	int groundcontents;

	int ladder;

	/* box, mask and touch recording of the current slide */
	const float *smins, *smaxs;
	int smask;
	int notouch;
} pmx_t;

/* ---- vector helpers (single precision, Quake II evaluation order) ------ */

static float
Dot(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float
Normalize(float *v)
{
	float length, ilength;

	length = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	length = sqrtf(length);

	if (length)
	{
		ilength = 1.0f / length;
		v[0] *= ilength;
		v[1] *= ilength;
		v[2] *= ilength;
	}

	return length;
}

static float
Length(const float *v)
{
	return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static void
Copy(const float *a, float *b)
{
	b[0] = a[0];
	b[1] = a[1];
	b[2] = a[2];
}

static void
Clear(float *v)
{
	v[0] = v[1] = v[2] = 0;
}

static float
DegToRad(float a)
{
	return (float)((double)a * (3.14159265358979323846 * 2.0 / 360.0));
}

static void
AngleVectors(const float *angles, float *forward, float *right, float *up)
{
	float angle, sr, sp, sy, cr, cp, cy;

	angle = DegToRad(angles[1]);
	sy = (float)sin(angle);
	cy = (float)cos(angle);
	angle = DegToRad(angles[0]);
	sp = (float)sin(angle);
	cp = (float)cos(angle);
	angle = DegToRad(angles[2]);
	sr = (float)sin(angle);
	cr = (float)cos(angle);

	forward[0] = cp * cy;
	forward[1] = cp * sy;
	forward[2] = -sp;

	right[0] = (-1 * sr * sp * cy + -1 * cr * -sy);
	right[1] = (-1 * sr * sp * sy + -1 * cr * cy);
	right[2] = -1 * sr * cp;

	up[0] = (cr * sp * cy + -sr * -sy);
	up[1] = (cr * sp * sy + -sr * cy);
	up[2] = cr * cp;
}

/* ---- callbacks ---------------------------------------------------------- */

static void
Trace(pmx_t *x, sofpm_trace_t *tr, const float *start, const float *mins,
		const float *maxs, const float *end, int mask)
{
	memset(tr, 0, sizeof(*tr));
	x->pm->trace(x->pm->ctx, tr, start, mins, maxs, end, mask);
}

static int
PointContents(pmx_t *x, const float *p)
{
	return x->pm->pointcontents(x->pm->ctx, p);
}

static void
DevPrint(pmx_t *x, const char *msg)
{
	if (x->pm->devprint)
	{
		x->pm->devprint(x->pm->ctx, msg);
	}
}

static void
AddTouch(pmx_t *x, void *ent)
{
	sofpm_t *pm = x->pm;

	if (ent && (pm->numtouch < SOFPM_MAXTOUCH))
	{
		pm->touchents[pm->numtouch++] = ent;
	}
}

static int
FreeSpace(pmx_t *x, const float *p, const float *mins, const float *maxs, int mask)
{
	sofpm_trace_t tr;

	Trace(x, &tr, p, mins, maxs, p, mask);
	return !tr.allsolid;
}

static int
FlyType(const sofpm_t *pm)
{
	return (pm->s.pm_type == 1) || (pm->s.pm_type == 2);
}

/* ---- Behaviour 14: emergency placement ---------------------------------- */

static void
EmergencyPlacement(pmx_t *x)
{
	sofpm_t *pm = x->pm;
	int dx, dy, dz, i;
	short c[3];
	float p[3];

	DevPrint(x, "EmergencySnapPosition()\n");

	for (dz = 10; dz >= -10; dz -= 2)
	{
		for (dy = 10; dy >= -10; dy -= 2)
		{
			for (dx = 10; dx >= -10; dx -= 2)
			{
				c[0] = (short)(x->fallback[0] + dx);
				c[1] = (short)(x->fallback[1] + dy);
				c[2] = (short)(x->fallback[2] + dz);

				for (i = 0; i < 3; i++)
				{
					p[i] = c[i] * 0.125f;
				}

				if (FlyType(pm) || FreeSpace(x, p, pm->mins, pm->maxs, pm->mask))
				{
					Copy(p, x->origin);

					for (i = 0; i < 3; i++)
					{
						x->fallback[i] = c[i];
						pm->s.origin[i] = c[i];
					}

					return;
				}
			}
		}
	}
}

/* ---- Behaviour 12: sliding along planes --------------------------------- */

static void
ClipVelocity(const float *in, const float *normal, float *out, float overbounce)
{
	float backoff, change;
	int i;

	backoff = Dot(in, normal) * overbounce;

	for (i = 0; i < 3; i++)
	{
		change = normal[i] * backoff;
		out[i] = in[i] - change;

		if ((out[i] > -STOP_EPSILON) && (out[i] < STOP_EPSILON))
		{
			out[i] = 0;
		}
	}
}

/* returns 0 when the slide ended through the all-solid placement */
static int
SlideMove(pmx_t *x)
{
	sofpm_t *pm = x->pm;
	float planes[MAX_CLIP_PLANES][3];
	float primal[3], end[3], c[3];
	float time_left, d;
	int bump, numplanes, i, j;
	sofpm_trace_t tr;

	Copy(x->velocity, primal);
	numplanes = 0;
	time_left = x->frametime;

	for (bump = 0; bump < 4; bump++)
	{
		for (i = 0; i < 3; i++)
		{
			end[i] = x->origin[i] + time_left * x->velocity[i];
		}

		Trace(x, &tr, x->origin, x->smins, x->smaxs, end, x->smask);

		if (tr.allsolid)
		{
			x->velocity[2] = 0;
			EmergencyPlacement(x);
			return 0;
		}

		if (tr.fraction > 0)
		{
			Copy(tr.endpos, x->origin);
			numplanes = 0;
		}

		if (tr.fraction == 1)
		{
			break;
		}

		if (!x->notouch)
		{
			AddTouch(x, tr.ent);
		}

		time_left -= time_left * tr.fraction;

		if (numplanes >= MAX_CLIP_PLANES)
		{
			Clear(x->velocity);
			break;
		}

		Copy(tr.normal, planes[numplanes]);
		numplanes++;

		/* OpenMerc (Q2 rebase, docs/DEVIATIONS.md): Quake II 3.19 PM_StepSlideMove_
		   as-is - clip against every plane until the velocity parallels all of
		   them, else go along the crease of two. SPEC-314 row 12's "rub" factor
		   (1 + 0.5 x dot, applied per move and only against planes[0]) is gone:
		   it made wall slides framerate-dependent (10-15 u/s diagonal slide at
		   165 fps) and dropped the second-plane check, so a corner could push
		   the player back into the first plane. */
		for (i = 0; i < numplanes; i++)
		{
			ClipVelocity(x->velocity, planes[i], x->velocity, OVERBOUNCE);

			for (j = 0; j < numplanes; j++)
			{
				if ((j != i) && (Dot(x->velocity, planes[j]) < 0))
				{
					break;
				}
			}

			if (j == numplanes)
			{
				break;
			}
		}

		if (i == numplanes)
		{
			if (numplanes != 2)
			{
				Clear(x->velocity);
				break;
			}

			c[0] = planes[0][1] * planes[1][2] - planes[0][2] * planes[1][1];
			c[1] = planes[0][2] * planes[1][0] - planes[0][0] * planes[1][2];
			c[2] = planes[0][0] * planes[1][1] - planes[0][1] * planes[1][0];
			d = Dot(c, x->velocity);

			for (i = 0; i < 3; i++)
			{
				x->velocity[i] = c[i] * d;
			}
		}

		if (Dot(x->velocity, primal) <= 0)
		{
			Clear(x->velocity);
			break;
		}
	}

	if (pm->s.pm_time)
	{
		Copy(primal, x->velocity);
	}

	return 1;
}

/* ---- Behaviour 13: step-slide ------------------------------------------- */

static void
StepSlideMove(pmx_t *x)
{
	float start_o[3], start_v[3], down_o[3], down_v[3], up[3], down[3];
	float dist_up, dist_down, d[3];
	sofpm_trace_t tr;
	int i;

	Copy(x->origin, start_o);
	Copy(x->velocity, start_v);

	SlideMove(x);

	Copy(x->origin, down_o);
	Copy(x->velocity, down_v);

	Copy(start_o, up);
	up[2] += STEPSIZE;

	if (!FreeSpace(x, up, x->smins, x->smaxs, x->smask))
	{
		return; /* keep the plain slide */
	}

	Copy(up, x->origin);
	Copy(start_v, x->velocity);

	SlideMove(x);

	/* push down the final amount */
	Copy(x->origin, down);
	down[2] -= STEPSIZE;
	Trace(x, &tr, x->origin, x->smins, x->smaxs, down, x->smask);

	if (!tr.allsolid)
	{
		Copy(tr.endpos, x->origin);
	}

	for (i = 0; i < 3; i++)
	{
		d[i] = x->origin[i] - up[i];
	}

	dist_up = Dot(d, start_v);

	for (i = 0; i < 3; i++)
	{
		d[i] = down_o[i] - start_o[i];
	}

	dist_down = Dot(d, start_v);

	if ((dist_up < dist_down) || (tr.normal[2] < MIN_STEP_NORMAL))
	{
		Copy(down_o, x->origin);
		Copy(down_v, x->velocity);
	}
	/* else: the stepped path with the raised slide's velocity (SPEC-314 row 16) */
}

/* ---- Behaviour 9 / 10: currents, ladder wish, acceleration -------------- */

static void
Accelerate(pmx_t *x, const float *wishdir, float wishspeed, float accel)
{
	float addspeed, accelspeed, currentspeed;
	int i;

	currentspeed = Dot(x->velocity, wishdir);
	addspeed = wishspeed - currentspeed;

	if (addspeed <= 0)
	{
		return;
	}

	accelspeed = accel * x->frametime * wishspeed;

	if (accelspeed > addspeed)
	{
		accelspeed = addspeed;
	}

	for (i = 0; i < 3; i++)
	{
		x->velocity[i] += accelspeed * wishdir[i];
	}
}

static void
CurrentDirection(int contents, float *v)
{
	Clear(v);

	if (contents & 0x40000)
	{
		v[0] += 1;
	}

	if (contents & 0x80000)
	{
		v[1] += 1;
	}

	if (contents & 0x100000)
	{
		v[0] -= 1;
	}

	if (contents & 0x200000)
	{
		v[1] -= 1;
	}

	if (contents & 0x400000)
	{
		v[2] += 1;
	}

	if (contents & 0x800000)
	{
		v[2] -= 1;
	}
}

static void
AddCurrents(pmx_t *x, float *wishvel)
{
	sofpm_t *pm = x->pm;
	float v[3], s;
	int i;

	/* ladder wish */
	if (x->ladder && (fabsf(x->velocity[2]) <= 200))
	{
		if ((pm->viewangles[0] <= -15) && (pm->cmd.forwardmove > 0))
		{
			wishvel[2] = 200;
		}
		else if ((pm->viewangles[0] >= 15) && (pm->cmd.forwardmove > 0))
		{
			wishvel[2] = -200;
		}
		else if (pm->cmd.upmove > 0)
		{
			wishvel[2] = 200;
		}
		else if (pm->cmd.upmove < 0)
		{
			wishvel[2] = -200;
		}
		else
		{
			wishvel[2] = 0;
		}

		for (i = 0; i < 2; i++)
		{
			if (wishvel[i] < -25)
			{
				wishvel[i] = -25;
			}
			else if (wishvel[i] > 25)
			{
				wishvel[i] = 25;
			}
		}
	}

	/* water currents */
	if (pm->watertype & MASK_CURRENT)
	{
		CurrentDirection(pm->watertype, v);
		s = 400;

		if ((pm->waterlevel == 1) && pm->groundentity)
		{
			s /= 2;
		}

		for (i = 0; i < 3; i++)
		{
			wishvel[i] = wishvel[i] + s * v[i];
		}
	}

	/* conveyors */
	if (pm->groundentity)
	{
		CurrentDirection(x->groundcontents, v);

		for (i = 0; i < 3; i++)
		{
			wishvel[i] = wishvel[i] + 100 * v[i];
		}
	}
}

static void
WaterMove(pmx_t *x)
{
	sofpm_t *pm = x->pm;
	float wishvel[3], wishdir[3], wishspeed;
	int i;

	for (i = 0; i < 3; i++)
	{
		wishvel[i] = x->forward[i] * pm->cmd.forwardmove + x->right[i] * pm->cmd.sidemove;
	}

	if (!pm->cmd.forwardmove && !pm->cmd.sidemove && !pm->cmd.upmove)
	{
		wishvel[2] -= 60;
	}
	else
	{
		wishvel[2] += pm->cmd.upmove;
	}

	AddCurrents(x, wishvel);

	Copy(wishvel, wishdir);
	wishspeed = Normalize(wishdir);

	if (wishspeed > 300)
	{
		for (i = 0; i < 3; i++)
		{
			wishvel[i] *= 300 / wishspeed;
		}

		wishspeed = 300;
	}

	wishspeed *= 0.5f;

	Accelerate(x, wishdir, wishspeed, 10 * x->k);
	StepSlideMove(x);
}


static void
AirMove(pmx_t *x)
{
	sofpm_t *pm = x->pm;
	float wishvel[3], wishdir[3], wishspeed, maxspeed, g;
	float fmove, smove;
	int i;

	fmove = pm->cmd.forwardmove;
	smove = pm->cmd.sidemove;

	x->forward[2] = 0;
	x->right[2] = 0;
	Normalize(x->forward);
	Normalize(x->right);

	for (i = 0; i < 2; i++)
	{
		wishvel[i] = x->forward[i] * fmove + x->right[i] * smove;
	}

	wishvel[2] = 0;

	AddCurrents(x, wishvel);

	Copy(wishvel, wishdir);
	wishspeed = Normalize(wishdir);

	maxspeed = ((pm->s.pm_flags & SOFPMF_DUCKED) && (pm->s.pm_flags & SOFPMF_ON_GROUND)) ? 75.0f : 300.0f;

	if (wishspeed > maxspeed)
	{
		for (i = 0; i < 3; i++)
		{
			wishvel[i] *= maxspeed / wishspeed;
		}

		wishspeed = maxspeed;
	}

	g = pm->s.gravity * x->frametime;

	if (x->ladder)
	{
		Accelerate(x, wishdir, wishspeed, 10 * x->k);

		if (!wishvel[2])
		{
			if (x->velocity[2] > 0)
			{
				x->velocity[2] -= g;

				if (x->velocity[2] < 0)
				{
					x->velocity[2] = 0;
				}
			}
			else
			{
				x->velocity[2] += g;

				if (x->velocity[2] > 0)
				{
					x->velocity[2] = 0;
				}
			}
		}

		StepSlideMove(x);
	}
	else if (pm->groundentity)
	{
		Accelerate(x, wishdir, wishspeed, 10 * x->k);
		x->velocity[2] = 0;

		if (!x->velocity[0] && !x->velocity[1])
		{
			return;
		}

		StepSlideMove(x);
	}
	else
	{
		Accelerate(x, wishdir, wishspeed, 1 * x->k);
		x->velocity[2] -= g;
		StepSlideMove(x);
	}
}

/* ---- Behaviour 4: ground and water classification ----------------------- */

static void
Categorize(pmx_t *x)
{
	sofpm_t *pm = x->pm;
	float point[3];
	int cont, sample1, sample2;
	sofpm_trace_t tr;

	if (x->velocity[2] > 100)
	{
		pm->s.pm_flags &= ~SOFPMF_ON_GROUND;
		pm->groundentity = NULL;
	}
	else
	{
		point[0] = x->origin[0];
		point[1] = x->origin[1];
		point[2] = x->origin[2] - 0.25f;

		Trace(x, &tr, x->origin, pm->mins, pm->maxs, point, pm->mask);
		x->groundsurfflags = tr.surfflags;
		x->groundcontents = tr.contents;

		if (tr.ent && ((tr.normal[2] >= 0.7f) || tr.startsolid))
		{
			pm->groundentity = tr.ent;

			if (pm->s.pm_flags & SOFPMF_TIME_WATERJUMP)
			{
				pm->s.pm_flags &= ~(SOFPMF_TIME_WATERJUMP | SOFPMF_TIME_LAND | SOFPMF_TIME_TELEPORT);
				pm->s.pm_time = 0;
			}

			if (!(pm->s.pm_flags & SOFPMF_ON_GROUND))
			{
				pm->s.pm_flags |= SOFPMF_ON_GROUND;

				if (x->velocity[2] < -200)
				{
					pm->s.pm_flags |= SOFPMF_TIME_LAND;
					pm->s.pm_time = (x->velocity[2] < -400) ? 25 : 18;
				}
			}
		}
		else
		{
			pm->groundentity = NULL;
			pm->s.pm_flags &= ~SOFPMF_ON_GROUND;
		}

		Copy(tr.normal, pm->probenormal);

		if (pm->debug)
		{
			char m[200];

			snprintf(m, sizeof(m), "  probe z %.4f frac %.4f ss %d as %d end %.4f n.z %.3f ent %d box %.0f..%.0f v (%.2f %.2f %.2f)\n",
					x->origin[2], tr.fraction, tr.startsolid, tr.allsolid, tr.endpos[2], tr.normal[2], tr.ent != NULL,
					pm->mins[2], pm->maxs[2], x->velocity[0], x->velocity[1], x->velocity[2]);
			DevPrint(x, m);
		}

		/* landing velocity (SPEC-314 row 13), OpenMerc (Q2 rebase,
		   docs/DEVIATIONS.md): only on ground. 1.07f zeroes vz on any probe hit,
		   so a too-steep slope never gains falling speed; Quake II lets the
		   player slide down it. */
		if (pm->groundentity && (tr.fraction < 1) && (x->velocity[2] < 0))
		{
			x->velocity[2] = 0;
		}

		AddTouch(x, tr.ent);
	}

	/* water */
	pm->waterlevel = 0;
	pm->watertype = 0;

	sample2 = (int)(pm->viewheight - pm->mins[2]);
	sample1 = sample2 / 2;

	point[0] = x->origin[0];
	point[1] = x->origin[1];
	point[2] = x->origin[2] + pm->mins[2] + 1;
	cont = PointContents(x, point);

	if (cont & MASK_WATER)
	{
		pm->watertype = cont;
		pm->waterlevel = 1;
		point[2] = x->origin[2] + pm->mins[2] + sample1;
		cont = PointContents(x, point);

		if (cont & MASK_WATER)
		{
			pm->waterlevel = 2;
			point[2] = x->origin[2] + pm->mins[2] + sample2;
			cont = PointContents(x, point);

			if (cont & MASK_WATER)
			{
				pm->waterlevel = 3;
			}
		}
	}
}

/* ---- Behaviour 5: ladder and water-jump start --------------------------- */

static void
SpecialMovement(pmx_t *x)
{
	sofpm_t *pm = x->pm;
	float spot[3], flatforward[3];
	sofpm_trace_t tr;
	int cont;

	if (pm->s.pm_time)
	{
		return;
	}

	x->ladder = 0;

	flatforward[0] = x->forward[0];
	flatforward[1] = x->forward[1];
	flatforward[2] = 0;
	Normalize(flatforward);

	spot[0] = x->origin[0] + 1 * flatforward[0];
	spot[1] = x->origin[1] + 1 * flatforward[1];
	spot[2] = x->origin[2] + 1 * flatforward[2];
	Trace(x, &tr, x->origin, pm->mins, pm->maxs, spot, pm->mask);

	if ((tr.fraction < 1) && (tr.contents & CONTENTS_LADDER))
	{
		x->ladder = 1;
	}

	if (pm->waterlevel != 2)
	{
		return;
	}

	spot[0] = x->origin[0] + 30 * flatforward[0];
	spot[1] = x->origin[1] + 30 * flatforward[1];
	spot[2] = x->origin[2] + 30 * flatforward[2];
	spot[2] += 4;
	cont = PointContents(x, spot);

	if (!(cont & CONTENTS_SOLID))
	{
		return;
	}

	spot[2] += 16;
	cont = PointContents(x, spot);

	if (cont)
	{
		return;
	}

	x->velocity[0] = 200 * x->forward[0];
	x->velocity[1] = 200 * x->forward[1];
	x->velocity[2] = 350;
	pm->s.pm_flags |= SOFPMF_TIME_WATERJUMP;
	pm->s.pm_time = 255;
}

/* ---- Behaviour 7 / 8: jump, friction ------------------------------------ */

static void
CheckJump(pmx_t *x)
{
	sofpm_t *pm = x->pm;

	if (pm->s.pm_flags & SOFPMF_TIME_LAND)
	{
		return;
	}

	if (pm->cmd.upmove < 10)
	{
		pm->s.pm_flags &= ~SOFPMF_JUMP_HELD;
		return;
	}

	if ((pm->s.pm_flags & SOFPMF_JUMP_HELD) || (pm->s.pm_type == 3))
	{
		return;
	}

	if (pm->waterlevel >= 2)
	{
		pm->groundentity = NULL;

		if (x->velocity[2] <= -300)
		{
			return;
		}

		if (pm->watertype == 0x20)
		{
			x->velocity[2] = 100;
		}
		else if (pm->watertype == 0x10)
		{
			x->velocity[2] = 80;
		}
		else
		{
			x->velocity[2] = 50;
		}

		return;
	}

	if (!pm->groundentity)
	{
		return;
	}

	if (pm->groundisclient && pm->groundisclient(pm->ctx, pm->groundentity))
	{
		return; /* standing on another player: refused */
	}

	pm->s.pm_flags |= SOFPMF_JUMP_HELD;
	pm->groundentity = NULL;
	x->velocity[2] = 270;
}

static void
Friction(pmx_t *x)
{
	sofpm_t *pm = x->pm;
	float speed, newspeed, control, drop;
	int i;

	speed = Length(x->velocity);

	if (speed < 1)
	{
		x->velocity[0] = 0;
		x->velocity[1] = 0;
		return;
	}

	drop = 0;

	if ((pm->groundentity && !(x->groundsurfflags & SURF_SLICK)) || x->ladder)
	{
		control = (speed < 100) ? 100 : speed;
		drop += control * 6 * x->frametime;
	}

	if (pm->waterlevel && !x->ladder)
	{
		drop += speed * 1 * pm->waterlevel * x->frametime;
	}

	newspeed = speed - drop;

	if (newspeed < 0)
	{
		newspeed = 0;
	}

	newspeed /= speed;

	for (i = 0; i < 3; i++)
	{
		x->velocity[i] = x->velocity[i] * newspeed;
	}
}

/* ---- Behaviour 16: noclip and spectator ---------------------------------- */

static void
FlyMove(pmx_t *x, int spectator)
{
	static const float smins[3] = {-5, -5, -2}, smaxs[3] = {5, 5, 8};
	sofpm_t *pm = x->pm;
	float speed, drop, control, newspeed, friction;
	float wishvel[3], wishdir[3], wishspeed, currentspeed, addspeed, accelspeed;
	float fmove, smove;
	int i;

	pm->viewheight = spectator ? 5.0f : 35.0f;

	speed = Length(x->velocity);

	if (speed < 1)
	{
		Clear(x->velocity);
	}
	else
	{
		drop = 0;
		friction = 6 * 1.5f;
		control = (speed < 100) ? 100 : speed;
		drop += control * friction * x->frametime;

		newspeed = speed - drop;

		if (newspeed < 0)
		{
			newspeed = 0;
		}

		newspeed /= speed;

		for (i = 0; i < 3; i++)
		{
			x->velocity[i] = x->velocity[i] * newspeed;
		}
	}

	fmove = pm->cmd.forwardmove;
	smove = pm->cmd.sidemove;

	Normalize(x->forward);
	Normalize(x->right);

	for (i = 0; i < 3; i++)
	{
		wishvel[i] = x->forward[i] * fmove + x->right[i] * smove;
	}

	wishvel[2] += pm->cmd.upmove;

	Copy(wishvel, wishdir);
	wishspeed = Normalize(wishdir);

	if (wishspeed > 300)
	{
		for (i = 0; i < 3; i++)
		{
			wishvel[i] *= 300 / wishspeed;
		}

		wishspeed = 300;
	}

	currentspeed = Dot(x->velocity, wishdir);
	addspeed = wishspeed - currentspeed;

	if (addspeed <= 0)
	{
		return;
	}

	accelspeed = 10 * x->frametime * wishspeed;

	if (accelspeed > addspeed)
	{
		accelspeed = addspeed;
	}

	for (i = 0; i < 3; i++)
	{
		x->velocity[i] += accelspeed * wishdir[i];
	}

	if (!spectator)
	{
		for (i = 0; i < 3; i++)
		{
			x->origin[i] = x->origin[i] + x->frametime * x->velocity[i];
		}

		return;
	}

	/* spectator: step-slide with the small box against the world only */
	x->smins = smins;
	x->smaxs = smaxs;
	x->smask = SOFPM_MASK_SPECTATOR;
	x->notouch = 1;
	StepSlideMove(x);
	x->smins = pm->mins;
	x->smaxs = pm->maxs;
	x->smask = pm->mask;
	x->notouch = 0;
}

/* ---- Behaviour 3: box, ducking, view height ------------------------------ */

static void
CheckDuck(pmx_t *x)
{
	sofpm_t *pm = x->pm;
	float standvh, duckvh, s;

	pm->mins[0] = -16;
	pm->mins[1] = -16;
	pm->maxs[0] = 16;
	pm->maxs[1] = 16;

	if (pm->s.pm_type == 4)
	{
		pm->mins[2] = 0;
		pm->maxs[2] = 16;
		pm->viewheight = 8;
		return;
	}

	pm->mins[2] = -24;

	if (pm->s.pm_type == 3)
	{
		pm->s.pm_flags |= SOFPMF_DUCKED;
	}
	else if (pm->cmd.upmove < 0)
	{
		if (!(pm->cmd.lean && !(pm->s.pm_flags & SOFPMF_DUCKED)))
		{
			pm->s.pm_flags |= SOFPMF_DUCKED;
		}
	}
	else if ((pm->s.pm_flags & SOFPMF_DUCKED) && !pm->cmd.lean)
	{
		pm->maxs[2] = 40;

		if (FreeSpace(x, x->origin, pm->mins, pm->maxs, pm->mask))
		{
			pm->s.pm_flags &= ~SOFPMF_DUCKED;
		}
	}

	s = pm->sizematters;

	if ((s <= 0) || (s == 1.0f))
	{
		standvh = 35;
		duckvh = 0;
	}
	else
	{
		standvh = s * 59 - 24;
		standvh = (standvh < -8) ? -8 : ((standvh > 39) ? 39 : standvh);
		duckvh = s * 24 - 24;
		duckvh = (duckvh < -10) ? -10 : ((duckvh > 13) ? 13 : duckvh);
	}

	if (pm->s.pm_flags & SOFPMF_DUCKED)
	{
		pm->maxs[2] = 14;
		pm->viewheight = duckvh;
	}
	else
	{
		pm->maxs[2] = 40;
		pm->viewheight = standvh;
	}
}

static void
DeadMove(pmx_t *x)
{
	float forward;
	int i;

	if (!x->pm->groundentity)
	{
		return;
	}

	forward = Length(x->velocity);
	forward -= 20;

	if (forward <= 0)
	{
		Clear(x->velocity);
	}
	else
	{
		Normalize(x->velocity);

		for (i = 0; i < 3; i++)
		{
			x->velocity[i] *= forward;
		}
	}
}

/* ---- Behaviour 15: snapping ---------------------------------------------- */

static int
GoodPosition(pmx_t *x, const short *o)
{
	float p[3];
	int i;

	if (FlyType(x->pm))
	{
		return 1;
	}

	for (i = 0; i < 3; i++)
	{
		p[i] = o[i] * 0.125f;
	}

	return FreeSpace(x, p, x->pm->mins, x->pm->maxs, x->pm->mask);
}

static void
FinalSnap(pmx_t *x)
{
	static const int jitterbits[8] = {0, 4, 1, 2, 3, 5, 6, 7};
	static const int groundbits[8] = {4, 0, 1, 2, 3, 5, 6, 7};
	sofpm_t *pm = x->pm;
	const int *order;
	short base[3], c[3];
	int sign[3];
	int i, j, bits;

	for (i = 0; i < 3; i++)
	{
		pm->s.velocity[i] = (short)(int)(x->velocity[i] * 8);
	}

	for (i = 0; i < 3; i++)
	{
		sign[i] = (x->origin[i] >= 0) ? 1 : -1;
		base[i] = (short)(int)(x->origin[i] * 8);

		if (base[i] * 0.125f == x->origin[i])
		{
			sign[i] = 0;
		}
	}

	/* OpenMerc (docs/DEVIATIONS.md): on ground with a negative z the 1/8 grid
	   value truncated toward zero lies ABOVE the box; tried first it lifts the
	   player up to 1/8 u, from a rest 0.125-0.25 u above the floor out of the
	   0.25 u ground probe. The next move then starts airborne (no friction, no
	   jump), falls < 1/8 u in a 6 ms move (165 fps), is lifted back and lands
	   again at its end: a permanent hover - the "stuck to the floor, sliding,
	   cannot jump" state (kos3 -863 16 -28, trn1). On ground the floor-side
	   value is tried first, so the snap never moves the box off the floor. */
	order = (pm->groundentity && (sign[2] < 0)) ? groundbits : jitterbits;

	for (j = 0; j < 8; j++)
	{
		bits = order[j];

		for (i = 0; i < 3; i++)
		{
			c[i] = base[i];

			if (bits & (1 << i))
			{
				c[i] = (short)(c[i] + sign[i]);
			}
		}

		if (GoodPosition(x, c))
		{
			for (i = 0; i < 3; i++)
			{
				pm->s.origin[i] = c[i];
			}

			return;
		}
	}

	for (i = 0; i < 3; i++)
	{
		pm->s.origin[i] = x->fallback[i];
	}
}

static void
InitialSnap(pmx_t *x)
{
	static const int offset[3] = {1, 0, -1};
	sofpm_t *pm = x->pm;
	short base[3], c[3];
	int dx, dy, dz, i;

	for (i = 0; i < 3; i++)
	{
		base[i] = pm->s.origin[i];
	}

	for (dz = 0; dz < 3; dz++)
	{
		for (dy = 0; dy < 3; dy++)
		{
			for (dx = 0; dx < 3; dx++)
			{
				c[0] = (short)(base[0] + offset[dx]);
				c[1] = (short)(base[1] + offset[dy]);
				c[2] = (short)(base[2] + offset[dz]);

				if (GoodPosition(x, c))
				{
					for (i = 0; i < 3; i++)
					{
						pm->s.origin[i] = c[i];
						x->fallback[i] = c[i];
						x->origin[i] = c[i] * 0.125f;
					}

					return;
				}
			}
		}
	}

	DevPrint(x, "Bad InitialSnapPosition\n");
}

/* ---- Behaviour 2: view angles -------------------------------------------- */

static void
ClampAngles(pmx_t *x)
{
	sofpm_t *pm = x->pm;
	short temp;
	int i;

	if (pm->s.pm_flags & SOFPMF_TIME_TELEPORT)
	{
		pm->viewangles[0] = 0;
		pm->viewangles[1] = (pm->cmd.angles[1] + pm->s.delta_angles[1]) * ANGLE_UNIT;
		pm->viewangles[2] = 0;
	}
	else
	{
		for (i = 0; i < 3; i++)
		{
			temp = (short)(pm->cmd.angles[i] + pm->s.delta_angles[i]);
			pm->viewangles[i] = temp * ANGLE_UNIT;
		}

		if ((pm->viewangles[0] > 89) && (pm->viewangles[0] < 180))
		{
			pm->viewangles[0] = 89;
		}
	}

	AngleVectors(pm->viewangles, x->forward, x->right, x->up);
}

/* ---- Behaviour 1: one move ------------------------------------------------ */

void
SoFPM_Move(sofpm_t *pm)
{
	pmx_t x;
	int i, type;

	memset(&x, 0, sizeof(x));
	x.pm = pm;

	pm->numtouch = 0;
	Clear(pm->viewangles);
	pm->viewheight = 0;
	pm->groundentity = NULL;
	pm->watertype = 0;
	pm->waterlevel = 0;

	if (!pm->mask)
	{
		pm->mask = SOFPM_MASK_PLAYERSOLID;
	}

	for (i = 0; i < 3; i++)
	{
		x.origin[i] = pm->s.origin[i] * 0.125f;
		x.velocity[i] = pm->s.velocity[i] * 0.125f;
		x.fallback[i] = pm->s.origin[i];
	}

	/* msec x 0.001 in double, rounded once: 10 ms -> 0.0099999998 */
	x.frametime = (float)(pm->cmd.msec * 0.001);
	x.k = 1;

	if (pm->stagger > 0)
	{
		x.k = 1 - pm->stagger;

		if (x.k < 0)
		{
			x.k = 0;
		}
	}

	x.smins = pm->mins;
	x.smaxs = pm->maxs;
	x.smask = pm->mask;

	ClampAngles(&x);

	type = pm->s.pm_type;

	if ((type == 1) || (type == 2))
	{
		FlyMove(&x, type == 2);
		FinalSnap(&x);
		return;
	}

	if ((type >= 3) && (type <= 7))
	{
		pm->cmd.forwardmove = 0;
		pm->cmd.sidemove = 0;
		pm->cmd.upmove = 0;
	}

	if ((type == 5) || (type == 7))
	{
		return;
	}

	CheckDuck(&x);

	if (pm->snapinitial)
	{
		InitialSnap(&x);
	}

	Categorize(&x);

	if (type == 3)
	{
		DeadMove(&x);
	}

	SpecialMovement(&x);

	/* timer */
	if (pm->s.pm_time)
	{
		int msec = pm->cmd.msec >> 3;

		if (!msec)
		{
			msec = 1;
		}

		if (msec >= pm->s.pm_time)
		{
			pm->s.pm_flags &= ~(SOFPMF_TIME_WATERJUMP | SOFPMF_TIME_LAND | SOFPMF_TIME_TELEPORT);
			pm->s.pm_time = 0;
		}
		else
		{
			pm->s.pm_time = (uint8_t)(pm->s.pm_time - msec);
		}
	}

	if (pm->s.pm_flags & SOFPMF_TIME_TELEPORT)
	{
		/* teleport pause: nothing moves */
	}
	else if (pm->s.pm_flags & SOFPMF_TIME_WATERJUMP)
	{
		x.velocity[2] -= pm->s.gravity * x.frametime;

		if (x.velocity[2] < 0)
		{
			pm->s.pm_flags &= ~(SOFPMF_TIME_WATERJUMP | SOFPMF_TIME_LAND | SOFPMF_TIME_TELEPORT);
			pm->s.pm_time = 0;
		}

		StepSlideMove(&x);
	}
	else if (type != 6)
	{
		CheckJump(&x);
		Friction(&x);

		if (pm->waterlevel >= 2)
		{
			WaterMove(&x);
		}
		else
		{
			AirMove(&x);
		}
	}

	Categorize(&x);
	FinalSnap(&x);
}

/* ---- sections 19 / 20: command expansion ---------------------------------- */

int
SoFPM_RunCommand(sofpm_t *pm, const sof_usercmd_t *in, int flags, int q)
{
	sof_usercmd_t c = *in;
	float scale;
	int msec, n, first, i;

	if ((c.buttons & SOFPM_RUN_BUTTON) && !(flags & SOFPMF_EXHAUSTED))
	{
		c.forwardmove = (int16_t)(c.forwardmove * 2);
		c.sidemove = (int16_t)(c.sidemove * 2);
		c.upmove = (int16_t)(c.upmove * 2);
	}

	scale = (float)(q & 255) * (1.0f / 255.0f);
	c.forwardmove = (int16_t)(int)(c.forwardmove * scale);
	c.sidemove = (int16_t)(int)(c.sidemove * scale);

	msec = c.msec;

	if ((msec > 33) && (pm->s.velocity[2] != 0))
	{
		n = msec / 10;
		first = (msec % 10) + 10;
	}
	else
	{
		n = 1;
		first = msec;
	}

	for (i = 0; i < n; i++)
	{
		pm->cmd = c;
		pm->cmd.msec = (uint8_t)(i ? 10 : first);
		SoFPM_Move(pm);
	}

	return n;
}
