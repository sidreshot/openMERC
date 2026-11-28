/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * B3: living flame and smoke (r_om_fxheat). The stock cards still decide when,
 * where, how big, what colour and how long. This file only decides which of
 * those cards are hot, and whether the high picture path can draw the boil
 * and the heat haze. The draw itself stays in gl3_fx.c and the effect shader.
 * r_om_fxheat 0, Low, Minimal, Medium and the GLES renderer keep the original
 * sprite: grow and fade, no boil, no haze.
 */

#include "header/local.h"

static cvar_t *r_om_fxheat;
static int heatcopied;
static int heatbusy;
static int resolved;
static float told = -999.0f;

/* A name is classified here, before any GL call, so a cold card cannot pick
   up the haze by accident. The first matching group wins. Muzzle flashes and
   sparks stay cold: they are not a fire that heats the air. */
int
GL3_FxHeat_Kind(const char *name)
{
	if (!name || !name[0])
	{
		return 0;
	}

	if (Q_strcasestr(name, "muzzle") || Q_strcasestr(name, "flash") || Q_strcasestr(name, "spark") ||
		Q_strcasestr(name, "tracer") || Q_strcasestr(name, "blood") || Q_strcasestr(name, "dust") ||
		Q_strcasestr(name, "debris") || Q_strcasestr(name, "snow") || Q_strcasestr(name, "water") ||
		Q_strcasestr(name, "splash") || Q_strcasestr(name, "powder") || Q_strcasestr(name, "paper") ||
		Q_strcasestr(name, "shard") || Q_strcasestr(name, "chunk") || Q_strcasestr(name, "gore"))
	{
		return 0;
	}

	if (Q_strcasestr(name, "fire") || Q_strcasestr(name, "flame") || Q_strcasestr(name, "flare") ||
		Q_strcasestr(name, "burn") || Q_strcasestr(name, "lava") || Q_strcasestr(name, "ember") ||
		Q_strcasestr(name, "phos") || Q_strcasestr(name, "magma"))
	{
		return 2; /* boil and heat haze */
	}

	if (Q_strcasestr(name, "smoke") || Q_strcasestr(name, "fume") || Q_strcasestr(name, "steam"))
	{
		return 1; /* boil only */
	}

	return 0;
}

/* -1 follows the preset already in the config: on for High and for an unset
   desktop preset (that one keeps the full picture), off otherwise. A saved
   0 or 1 is left alone. GLES never resolves to on. */
void
GL3_FxHeat_Resolve(void)
{
	cvar_t *preset;
	int p;

	if (resolved)
	{
		return;
	}

	if (!r_om_fxheat)
	{
		r_om_fxheat = ri.Cvar_Get("r_om_fxheat", "-1", CVAR_ARCHIVE);
	}

	if (r_om_fxheat->value >= 0.0f)
	{
		resolved = 1;
		return;
	}

#ifdef YQ2_GL3_GLES3
	ri.Cvar_Set("r_om_fxheat", "0");
	(void)preset;
	(void)p;
#else
	preset = ri.Cvar_Get("r_om_preset", "0", CVAR_ARCHIVE);
	p = (int)preset->value;
	ri.Cvar_Set("r_om_fxheat", ((p == 0) || (p == 3)) ? "1" : "0");
#endif
	resolved = 1;
}

qboolean
GL3_FxHeat_Active(void)
{
	qboolean on;

	GL3_FxHeat_Resolve();

#ifdef YQ2_GL3_GLES3
	on = false;
#else
	on = r_om_fxheat && (r_om_fxheat->value > 0.0f) && GL3_Post_DepthCopyPossible();
#endif

	if (r_om_fxheat && (r_om_fxheat->value != told))
	{
		told = r_om_fxheat->value;
		R_Printf(PRINT_ALL, "r_om_fxheat: %s\n", on ? "on" : "off");
	}

	return on;
}

void
GL3_FxHeat_BeginFrame(void)
{
	heatcopied = 0;
	heatbusy = 0;
}

qboolean
GL3_FxHeat_Velocity(const sofalphaquad_t *q, vec3_t vel)
{
	float f[3];

	if (!q || (q->engine[0] != OM_FXHEAT_MAGIC))
	{
		return false;
	}

	memcpy(f, q->engine + 4, sizeof(f));
	VectorCopy(f, vel);
	return true;
}

/* The scene drawn so far, once per frame, for the haze. False when the post
   path is not drawing this view: the caller then keeps the original sprite. */
qboolean
GL3_FxHeat_EnsureCopy(void)
{
	if (!GL3_FxHeat_Active())
	{
		return false;
	}

	if (heatcopied)
	{
		return true;
	}

	if (heatbusy)
	{
		return false;
	}

	heatbusy = 1;
	GL3_FX_Flush();
	heatcopied = GL3_Post_GlassCopy(NULL) ? 1 : 0;
	heatbusy = 0;
	return heatcopied != 0;
}

void
GL3_FxHeat_Bind(void)
{
	GL3_Post_GlassBind();
}

void
GL3_FxHeat_Feed(float out[4], int mode)
{
	float invw = 0, invh = 0;

	out[0] = out[1] = out[2] = out[3] = 0;

	if (!GL3_FxHeat_Active() || (mode == 0))
	{
		return;
	}

	GL3_Post_ViewInv(&invw, &invh);

	if ((mode == 3) && ((invw <= 0.0f) || !heatcopied))
	{
		return; /* no colour to refract: the haze draw is skipped by the caller */
	}

	out[0] = (float)mode;
	out[2] = invw;
	out[3] = invh;
}
