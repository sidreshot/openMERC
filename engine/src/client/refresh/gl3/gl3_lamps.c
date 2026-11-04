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
} lamp_t;

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
		numlamps++;
	}
}

/* the lamps nearest in influence to the eye go to the shader: omBL[i] = origin, radius; omBC[i] = colour */
void
GL3_Lamps_View(GLuint prog, const float *eye)
{
	float pos[LAMPS_MAX_VIEW * 4], col[LAMPS_MAX_VIEW * 4], score[LAMPS_MAX_VIEW];
	int i, j, n = 0;
	GLint loc;

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

		/* insertion into the best LAMPS_MAX_VIEW by score */
		for (j = n; j > 0 && score[j - 1] < s; j--)
		{
			if (j < LAMPS_MAX_VIEW)
			{
				score[j] = score[j - 1];
				memcpy(pos + j * 4, pos + (j - 1) * 4, sizeof(float) * 4);
				memcpy(col + j * 4, col + (j - 1) * 4, sizeof(float) * 4);
			}
		}

		if (j >= LAMPS_MAX_VIEW)
		{
			continue;
		}

		score[j] = s;
		pos[j * 4 + 0] = l->origin[0];
		pos[j * 4 + 1] = l->origin[1];
		pos[j * 4 + 2] = l->origin[2];
		pos[j * 4 + 3] = l->radius;
		col[j * 4 + 0] = l->color[0];
		col[j * 4 + 1] = l->color[1];
		col[j * 4 + 2] = l->color[2];
		col[j * 4 + 3] = 1.0f;

		if (n < LAMPS_MAX_VIEW)
		{
			n++;
		}
	}

	loc = GL3_UniformLoc(prog, "omBL");
	if (loc != -1 && n > 0)
	{
		glUniform4fv(loc, n, pos);
	}

	loc = GL3_UniformLoc(prog, "omBC");
	if (loc != -1 && n > 0)
	{
		glUniform4fv(loc, n, col);
	}

	loc = GL3_UniformLoc(prog, "omBN");
	if (loc != -1)
	{
		glUniform1i(loc, n);
	}
}
