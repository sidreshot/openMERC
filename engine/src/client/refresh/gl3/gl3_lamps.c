/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GFX-modern: the map's lamps (lump 20 of the re-baked world) for
 * the lamp glints of the screen-space reflections (r_om_ssr_glint, gl3_post.c).
 */

#include "header/local.h"

#define LAMPS_MAX_VIEW 8

typedef struct
{
	vec3_t origin;
	vec3_t color;
	float radius;
	float fade;   /* 0..1: the lamp's glints coming (picked) or going (dropped) - never at once */
} lamp_t;

#define LAMPS_FADE 0.4f /* seconds */

static lamp_t *lamps;
static int numlamps;

/* the static lights of the new world: origin, colour, intensity, type (0 surface patch, 1 point, 2 spot, 3 sky) */
void
GL3_Lamps_Set(const byte *lights, int len)
{
	int i, n = len / 36;

	free(lamps);
	lamps = NULL;
	numlamps = 0;

	if (n <= 0)
	{
		return;
	}

	lamps = malloc(sizeof(lamp_t) * n);

	if (!lamps)
	{
		return;
	}

	for (i = 0; i < n; i++)
	{
		const byte *r = lights + 36 * i;
		float intensity = LittleFloat(*(const float *)(r + 24));
		int type = LittleLong(*(const int *)(r + 28));
		lamp_t *l = &lamps[numlamps];
		float m;

		if ((intensity <= 0) || (type == 3))
		{
			continue;
		}

		l->origin[0] = LittleFloat(*(const float *)(r + 0));
		l->origin[1] = LittleFloat(*(const float *)(r + 4));
		l->origin[2] = LittleFloat(*(const float *)(r + 8));
		l->color[0] = LittleFloat(*(const float *)(r + 12));
		l->color[1] = LittleFloat(*(const float *)(r + 16));
		l->color[2] = LittleFloat(*(const float *)(r + 20));
		m = fmaxf(l->color[0], fmaxf(l->color[1], l->color[2]));

		if (m > 0)
		{
			VectorScale(l->color, 1.0f / m, l->color);
		}

		/* reach: points their intensity, surface patches more (their light falls off as 1/d) */
		l->radius = (type == 0) ? fminf(sqrtf(intensity) * 8.0f, 600.0f) : fminf(intensity, 800.0f);
		l->fade = 0;
		numlamps++;
	}
}

/* true when the segment a-b passes through solid world (the BSP of the world model) */
static qboolean
SolidBetween(mnode_t *node, const vec3_t a, const vec3_t b)
{
	float fa, fb, frac;
	vec3_t mid;
	int side;

	if (node->contents != CONTENTS_NODE)
	{
		return (node->contents & CONTENTS_SOLID) != 0;
	}

	fa = DotProduct(a, node->plane->normal) - node->plane->dist;
	fb = DotProduct(b, node->plane->normal) - node->plane->dist;

	if ((fa >= 0) && (fb >= 0))
	{
		return SolidBetween(node->children[0], a, b);
	}

	if ((fa < 0) && (fb < 0))
	{
		return SolidBetween(node->children[1], a, b);
	}

	side = (fa < 0);
	frac = fa / (fa - fb);
	mid[0] = a[0] + (b[0] - a[0]) * frac;
	mid[1] = a[1] + (b[1] - a[1]) * frac;
	mid[2] = a[2] + (b[2] - a[2]) * frac;

	return SolidBetween(node->children[side], a, mid) || SolidBetween(node->children[!side], mid, b);
}

/* can the lamp's light reach what the eye sees of the glossy faces around it: a clear line from the lamp (16 units
   out of its fixture, toward the eye) to the eye, or to the point 40 units under the eye (a floor's glint is seen
   from a little above it). A lamp behind a wall gave glints on the near side of the wall. */
static qboolean
LampSeen(const lamp_t *l, const float *eye)
{
	vec3_t d, a, low;
	float len;

	if (!gl3_worldmodel || !gl3_worldmodel->nodes)
	{
		return true;
	}

	VectorSubtract(eye, l->origin, d);
	len = VectorLength(d);

	if (len < 24.0f)
	{
		return true;
	}

	VectorMA(l->origin, 16.0f / len, d, a);

	if (!SolidBetween(gl3_worldmodel->nodes, a, eye))
	{
		return true;
	}

	VectorCopy(eye, low);
	low[2] -= 40.0f;

	return !SolidBetween(gl3_worldmodel->nodes, a, low);
}

/* the lamps nearest in influence to the eye go to the shader: omBL[i] = origin, radius; omBC[i] = colour */
void
GL3_Lamps_View(GLuint prog, const float *eye)
{
	/* the best LAMPS_MAX_VIEW lamps (light over distance, seen from the eye) are the target; every lamp fades toward
	   being in it or not over LAMPS_FADE - picked at once, a lamp turning a corner made its glints on the floor
	   appear and vanish in the middle of the room. A fading lamp keeps its place until it is out */
	static float lasttime = -1;
	float pos[LAMPS_MAX_VIEW * 4], col[LAMPS_MAX_VIEW * 4], score[LAMPS_MAX_VIEW], dt;
	int pick[LAMPS_MAX_VIEW], out[LAMPS_MAX_VIEW];
	int i, j, n = 0, no = 0;
	GLint loc;

	dt = ((lasttime < 0) || (r_newrefdef.time < lasttime)) ? 1.0f : Q_min(r_newrefdef.time - lasttime, 0.1f);
	lasttime = r_newrefdef.time;

	for (i = 0; i < numlamps; i++)
	{
		const lamp_t *l = &lamps[i];
		vec3_t d;
		float dist, s;

		VectorSubtract(l->origin, eye, d);
		dist = VectorLength(d);

		if (dist > l->radius + 700.0f)
		{
			continue;
		}

		s = l->radius / (dist + 64.0f);

		/* the score first: the line test only for lamps that would get a place */
		if ((n >= LAMPS_MAX_VIEW) && (s <= score[LAMPS_MAX_VIEW - 1]))
		{
			continue;
		}

		if (!LampSeen(l, eye))
		{
			continue;
		}

		for (j = n; j > 0 && score[j - 1] < s; j--)
		{
			if (j < LAMPS_MAX_VIEW)
			{
				score[j] = score[j - 1];
				pick[j] = pick[j - 1];
			}
		}

		if (j >= LAMPS_MAX_VIEW)
		{
			continue;
		}

		score[j] = s;
		pick[j] = i;

		if (n < LAMPS_MAX_VIEW)
		{
			n++;
		}
	}

	/* the fades; what is lit: the faded-in lamps, the strongest first */
	for (i = 0; i < numlamps; i++)
	{
		lamp_t *l = &lamps[i];
		qboolean want = false;
		vec3_t d;
		float s;

		for (j = 0; j < n; j++)
		{
			want |= (pick[j] == i);
		}

		l->fade = Q_clamp(l->fade + (want ? dt : -dt) / LAMPS_FADE, 0.0f, 1.0f);

		if (l->fade <= 0)
		{
			continue;
		}

		VectorSubtract(l->origin, eye, d);
		s = l->fade * l->radius / (VectorLength(d) + 64.0f);

		for (j = no; j > 0 && score[j - 1] < s; j--)
		{
			if (j < LAMPS_MAX_VIEW)
			{
				score[j] = score[j - 1];
				out[j] = out[j - 1];
			}
		}

		if (j >= LAMPS_MAX_VIEW)
		{
			continue;
		}

		score[j] = s;
		out[j] = i;

		if (no < LAMPS_MAX_VIEW)
		{
			no++;
		}
	}

	for (j = 0; j < no; j++)
	{
		const lamp_t *l = &lamps[out[j]];
		float k = l->fade * l->fade * (3.0f - 2.0f * l->fade);

		pos[j * 4 + 0] = l->origin[0];
		pos[j * 4 + 1] = l->origin[1];
		pos[j * 4 + 2] = l->origin[2];
		pos[j * 4 + 3] = l->radius;
		col[j * 4 + 0] = l->color[0] * k;
		col[j * 4 + 1] = l->color[1] * k;
		col[j * 4 + 2] = l->color[2] * k;
		col[j * 4 + 3] = 1.0f;
	}

	loc = GL3_UniformLoc(prog, "omBL");
	if (loc != -1 && no > 0)
	{
		glUniform4fv(loc, no, pos);
	}

	loc = GL3_UniformLoc(prog, "omBC");
	if (loc != -1 && no > 0)
	{
		glUniform4fv(loc, no, col);
	}

	loc = GL3_UniformLoc(prog, "omBN");
	if (loc != -1)
	{
		glUniform1i(loc, no);
	}
}
