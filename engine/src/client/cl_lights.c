/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * This file implements all client side lighting
 *
 * =======================================================================
 *
 * OpenMerc: Soldier of Fortune dynamic lights (byte colours, lifetime in ms,
 * decay, the Quake II "stop decaying for this frame" rule) and light style
 * 13 (colour from configstring 5, lightning flash).
 * Implements: SPEC-400 (Dynamic lights, Light styles), SPEC-401 type 16
 */

#include "header/client.h"

typedef struct
{
	int length;
	float value[3];
	float map[MAX_QPATH];
} clightstyle_t;

clightstyle_t cl_lightstyle[MAX_LIGHTSTYLES];
int lastofs;

void
CL_ClearLightStyles(void)
{
	memset(cl_lightstyle, 0, sizeof(cl_lightstyle));
	lastofs = -1;
}

/* OpenMerc: forward declarations for the SoF style 13 state below */
static void CL_SoF_ResetStyle13(void);

void
CL_RunLightStyles(void)
{
	int ofs;
	int i;
	clightstyle_t *ls;

	ofs = cl.time / 100;

	if (ofs == lastofs)
	{
		return;
	}

	lastofs = ofs;

	for (i = 0, ls = cl_lightstyle; i < MAX_LIGHTSTYLES; i++, ls++)
	{
		if (!ls->length)
		{
			ls->value[0] = ls->value[1] = ls->value[2] = 1.0;
			continue;
		}

		if (ls->length == 1)
		{
			ls->value[0] = ls->value[1] = ls->value[2] = ls->map[0];
		}

		else
		{
			ls->value[0] = ls->value[1] = ls->value[2] = ls->map[ofs % ls->length];
		}
	}
}

void
CL_SetLightstyle(int i)
{
	char *s;
	int j, k;

	s = cl.configstrings[i + CS_LIGHTS];

	j = (int)strlen(s);
	cl_lightstyle[i].length = j;

	for (k = 0; k < j; k++)
	{
		cl_lightstyle[i].map[k] = (float)(s[k] - 'a') / (float)('m' - 'a');
	}
}

/* OpenMerc: style 13 (SPEC-400 Light styles) */
static float sof_style13[3];
static float sof_flash[3];
static int sof_flash_start, sof_flash_end;

void
CL_SoF_LightningFlash(float r, float g, float b, float ms)
{
	sof_flash[0] = r;
	sof_flash[1] = g;
	sof_flash[2] = b;
	sof_flash_start = cl.time;
	sof_flash_end = cl.time + (int)ms;
}

static void
CL_SoF_Style13(void)
{
	float v[3], f;
	int i;

	if (sscanf(CL_SoF_ConfigString(5), "%f %f %f", &v[0], &v[1], &v[2]) == 3)
	{
		for (i = 0; i < 3; i++)
		{
			sof_style13[i] = v[i]; /* otherwise the previous value stays */
		}
	}

	for (i = 0; i < 3; i++)
	{
		v[i] = sof_style13[i];
	}

	if ((cl.time < sof_flash_end) && (sof_flash_end > sof_flash_start))
	{
		f = (float)(sof_flash_end - cl.time) / (float)(sof_flash_end - sof_flash_start);

		for (i = 0; i < 3; i++)
		{
			v[i] = sof_flash[i] * f + sof_style13[i] * (1.0f - f);
		}
	}

	V_AddLightStyle(13, v[0], v[1], v[2]);
}

void
CL_AddLightStyles(void)
{
	int i;
	clightstyle_t *ls;

	for (i = 0, ls = cl_lightstyle; i < MAX_LIGHTSTYLES; i++, ls++)
	{
		if ((i == 13) && CL_SoF_Active())
		{
			CL_SoF_Style13();
			continue;
		}

		V_AddLightStyle(i, ls->value[0], ls->value[1], ls->value[2]);
	}
}

cdlight_t cl_dlights[MAX_DLIGHTS];

void
CL_ClearDlights(void)
{
	memset(cl_dlights, 0, sizeof(cl_dlights));
	CL_SoF_ResetStyle13();
}

static void
CL_SoF_ResetStyle13(void)
{
	/* black after a level change until configstring 5 parses */
	sof_style13[0] = sof_style13[1] = sof_style13[2] = 0;
	sof_flash_end = sof_flash_start = 0;
}

/* OpenMerc: a SoF dynamic light (SPEC-400 Dynamic lights item 1) */
int cl_sof_lights_added; /* count for the om_fxtrace summaries (fx_particle.c) */

/* lights fired during the effects steps are first drawn in the next frame: the original copies
   the lights at submission, before the steps (SPEC-AMEND-005 I5.2) */
static qboolean cl_sof_hold_lights;
static int cl_sof_light_from[MAX_DLIGHTS];

void
CL_SoF_HoldNewLights(qboolean hold)
{
	cl_sof_hold_lights = hold;
}

void
CL_SoF_AddLight(int key, const vec3_t org, int r, int g, int b, float radius,
		float life_ms, float decay)
{
	cdlight_t *dl = CL_AllocDlight(key);

	cl_sof_lights_added++; /* OpenMerc (I5): om_fxtrace summaries */
	cl_sof_light_from[dl - cl_dlights] = cl_sof_hold_lights ? cls.framecount + 1 : 0;
	VectorCopy(org, dl->origin);
	dl->color[0] = r / 256.0f;
	dl->color[1] = g / 256.0f;
	dl->color[2] = b / 256.0f;
	dl->radius = radius;
	dl->die = cl.time + life_ms;
	dl->decay = decay;
	dl->minlight = 0;
}

void
CL_SoF_ClearLights(void)
{
	CL_ClearDlights();
}

cdlight_t *
CL_AllocDlight(int key)
{
	int i;
	cdlight_t *dl;

	/* first look for an exact key match */
	if (key)
	{
		dl = cl_dlights;

		for (i = 0; i < MAX_DLIGHTS; i++, dl++)
		{
			if (dl->key == key)
			{
				cl_sof_light_from[dl - cl_dlights] = 0; /* OpenMerc (I5) */
				return dl;
			}
		}
	}

	/* then look for anything else */
	dl = cl_dlights;

	for (i = 0; i < MAX_DLIGHTS; i++, dl++)
	{
		if (dl->die < cl.time)
		{
			dl->key = key;
			cl_sof_light_from[dl - cl_dlights] = 0; /* OpenMerc (I5) */
			return dl;
		}
	}

	dl = &cl_dlights[0];
	dl->key = key;
	cl_sof_light_from[dl - cl_dlights] = 0; /* OpenMerc (I5) */
	return dl;
}

void
CL_NewDlight(int key, float x, float y, float z, float radius, float time)
{
	cdlight_t *dl;

	dl = CL_AllocDlight(key);
	dl->origin[0] = x;
	dl->origin[1] = y;
	dl->origin[2] = z;
	dl->radius = radius;
	dl->die = cl.time + time;
}

void
CL_RunDLights(void)
{
	int i;
	cdlight_t *dl;

	dl = cl_dlights;

	for (i = 0; i < MAX_DLIGHTS; i++, dl++)
	{
		if (!dl->radius)
		{
			continue;
		}

		/* OpenMerc: SoF keeps the Quake II rule - a light that died is
		   cleared and the remaining lights are not decayed this frame */
		if (CL_SoF_Active())
		{
			if (dl->die < cl.time)
			{
				dl->radius = 0;
				return;
			}

			dl->radius -= cls.rframetime * dl->decay;

			if (dl->radius < 0)
			{
				dl->radius = 0;
			}

			continue;
		}

		/* Vanilla Quake II had just cl.time. This worked in 1997
		   when computers were slow and the game reached ~30 FPS
		   on beefy hardware. Nowadays with 1000 FPS the dlights
		   are often rendered just a fraction of a frame. Work
		   around that by adding 32 ms, e.g. each dlight is shown
		   for at least 32 ms. */
		if (dl->die < cl.time - 32)
		{
			dl->radius = 0;
			continue;
		}

		dl->radius -= cls.rframetime * dl->decay;

		if (dl->radius < 0)
		{
			dl->radius = 0;
		}
	}
}

void
CL_AddDLights(void)
{
	int i;
	cdlight_t *dl;

	dl = cl_dlights;

	for (i = 0; i < MAX_DLIGHTS; i++, dl++)
	{
		if (!dl->radius)
		{
			continue;
		}

		if (cl_sof_light_from[i] > cls.framecount)
		{
			continue; /* OpenMerc (I5): fired during this frame's effects steps */
		}

		V_AddLight(dl->origin, dl->radius, dl->color[0], dl->color[1], dl->color[2]);
	}
}

