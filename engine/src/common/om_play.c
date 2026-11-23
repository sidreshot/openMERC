/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * See header/om_play.h.
 */

#include <string.h>

#include "header/om_play.h"

const char *
OM_IntroCvarName(void)
{
	return "om_showintro";
}

int
OM_IntroDefaultOn(void)
{
	return 1;
}

int
OM_IntroArchived(void)
{
	return 1;
}

static int
SameWord(const char *a, const char *b)
{
	if (!a || !b)
	{
		return 0;
	}

	while (*a && *b)
	{
		char ca = *a++;
		char cb = *b++;

		if (ca >= 'A' && ca <= 'Z')
		{
			ca = (char)(ca - 'A' + 'a');
		}

		if (cb >= 'A' && cb <= 'Z')
		{
			cb = (char)(cb - 'A' + 'a');
		}

		if (ca != cb)
		{
			return 0;
		}
	}

	return *a == 0 && *b == 0;
}

int
OM_IntroSkip(int showintro, const char *menu)
{
	if (showintro)
	{
		return 0;
	}

	if (SameWord(menu, "disclaimer") || SameWord(menu, "intro"))
	{
		return 1;
	}

	return 0;
}

int
OM_TitleFadeAlpha(int age_ms, int life_ms, int fade_ms)
{
	int fade;
	int start;
	int along;
	float u;
	float s;

	if (life_ms <= 0 || age_ms >= life_ms)
	{
		return 0;
	}

	if (age_ms < 0)
	{
		age_ms = 0;
	}

	if (fade_ms <= 0)
	{
		fade_ms = OM_TITLE_FADE_MS;
	}

	fade = fade_ms;

	if (fade > life_ms)
	{
		fade = life_ms;
	}

	if (fade <= 0)
	{
		return 0;
	}

	start = life_ms - fade;

	if (age_ms <= start)
	{
		return 255;
	}

	along = age_ms - start;

	if (along >= fade)
	{
		return 0;
	}

	u = (float)along / (float)fade;
	s = u * u * (3.0f - 2.0f * u);
	return (int)(255.0f * (1.0f - s) + 0.5f);
}

float
OM_CineSpawnTime(float spawn_time, float cine_start, float cine_end, int opening)
{
	if (!opening)
	{
		return spawn_time;
	}

	if (spawn_time < cine_start || spawn_time >= cine_end)
	{
		return spawn_time;
	}

	return spawn_time - OM_CINE_SPAWN_EARLY;
}
