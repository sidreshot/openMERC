/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Shipped startup-reel decision, title fade, and opening-cinematic spawn
 * shift. Exit code = number of failed checks.
 *
 * Usage: menu_fade_spawn_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "header/om_play.h"

static int failed;

static void
Check(int cond, const char *what)
{
	if (cond)
	{
		printf("PASS %s\n", what);
	}
	else
	{
		printf("FAIL %s\n", what);
		failed++;
	}
}

static char *
ReadFile(const char *path)
{
	FILE *f;
	long n;
	char *buf;

	f = fopen(path, "rb");

	if (!f)
	{
		return NULL;
	}

	if (fseek(f, 0, SEEK_END) != 0)
	{
		fclose(f);
		return NULL;
	}

	n = ftell(f);

	if (n < 0 || n > 1000000)
	{
		fclose(f);
		return NULL;
	}

	if (fseek(f, 0, SEEK_SET) != 0)
	{
		fclose(f);
		return NULL;
	}

	buf = (char *)malloc((size_t)n + 1);

	if (!buf)
	{
		fclose(f);
		return NULL;
	}

	if (fread(buf, 1, (size_t)n, f) != (size_t)n)
	{
		free(buf);
		fclose(f);
		return NULL;
	}

	buf[n] = 0;
	fclose(f);
	return buf;
}

static int
Exists(const char *path)
{
	FILE *f = fopen(path, "rb");

	if (!f)
	{
		return 0;
	}

	fclose(f);
	return 1;
}

/* Walk up from the process directory until the Gameplay menu files appear. */
static int
FindMenus(char *tbl, int tbln, char *rmf, int rmfn)
{
	char dir[640];
	int up;

	if (!getcwd(dir, (int)sizeof(dir)))
	{
		return 0;
	}

	for (up = 0; up < 8; up++)
	{
		size_t n;

		snprintf(tbl, (size_t)tbln, "%s/data/base/menus/om_assist.tbl", dir);
		snprintf(rmf, (size_t)rmfn, "%s/data/base/menus/m_scale.rmf", dir);

		if (Exists(tbl) && Exists(rmf))
		{
			return 1;
		}

		n = strlen(dir);

		if (n + 4 >= sizeof(dir))
		{
			return 0;
		}

		memcpy(dir + n, "/..", 4);
	}

	return 0;
}

static int
RowHasToggle(const char *text)
{
	const char *p = text;
	const char *name = OM_IntroCvarName();

	while ((p = strstr(p, name)) != NULL)
	{
		const char *line = p;

		while (line > text && line[-1] != '\n')
		{
			line--;
		}

		if (strstr(line, "list") == line || (line < p && strstr(line, "list")))
		{
			const char *end = strchr(p, '\n');
			char row[512];
			int n = end ? (int)(end - line) : (int)strlen(line);

			if (n > (int)sizeof(row) - 1)
			{
				n = (int)sizeof(row) - 1;
			}

			memcpy(row, line, (size_t)n);
			row[n] = 0;

			if (strstr(row, "list") && strstr(row, name) && strstr(row, "0,1") &&
				strstr(row, "Off,On"))
			{
				return 1;
			}
		}

		p += strlen(name);
	}

	return 0;
}

int
main(void)
{
	char tblpath[768], rmfpath[768];
	char *tbl, *rmf;
	int life = 4000;
	int start;
	int t;
	int prev;
	int mid;
	int biggest;
	float spawn;
	float shifted;
	float outside;
	float later;

	printf("menu_fade_spawn_test\n");

	Check(OM_IntroDefaultOn() == 1, "startup splash defaults on");
	Check(OM_IntroArchived() == 1, "startup splash control is archived");
	Check(strcmp(OM_IntroCvarName(), "om_showintro") == 0, "the control is om_showintro");

	Check(OM_IntroSkip(1, "disclaimer") == 0, "default on plays the disclaimer");
	Check(OM_IntroSkip(1, "intro") == 0, "default on plays the studio intro");
	Check(OM_IntroSkip(0, "disclaimer") == 1, "off skips the disclaimer");
	Check(OM_IntroSkip(0, "Intro") == 1, "off skips the studio intro");
	Check(OM_IntroSkip(0, "main") == 0, "off does not skip the main menu");
	Check(OM_IntroSkip(0, "sud1") == 0, "off does not skip an in-level menu");
	Check(OM_IntroSkip(0, "mail") == 0, "off does not skip the briefing");
	Check(OM_IntroSkip(0, "introcam") == 0, "off does not skip a mission cinematic");

	if (!FindMenus(tblpath, (int)sizeof(tblpath), rmfpath, (int)sizeof(rmfpath)))
	{
		Check(0, "gameplay menu files are reachable");
	}
	else
	{
		tbl = ReadFile(tblpath);
		rmf = ReadFile(rmfpath);
		Check(tbl && RowHasToggle(tbl), "settings menu data has the startup-splash row");
		Check(rmf && strstr(rmf, "menus/om_assist.tbl") != NULL,
				"the gameplay page includes that row");
		if (rmf)
		{
			const char *inc = strstr(rmf, "menus/om_assist.tbl");
			const char *view = strstr(rmf, "MENU_OMSET_SECTION_VIEW");

			Check(inc && view && inc < view,
					"the startup-splash row is the first block on the gameplay page");
		}
		free(tbl);
		free(rmf);
	}

	{
		char vid[768];
		char dir[640];
		char *video = NULL;
		int up;

		vid[0] = 0;

		if (getcwd(dir, (int)sizeof(dir)))
		{
			for (up = 0; up < 8; up++)
			{
				size_t n;

				snprintf(vid, sizeof(vid), "%s/data/base/menus/om_video.tbl", dir);

				if (Exists(vid))
				{
					break;
				}

				vid[0] = 0;
				n = strlen(dir);

				if (n + 4 >= sizeof(dir))
				{
					break;
				}

				memcpy(dir + n, "/..", 4);
			}
		}

		if (vid[0])
		{
			video = ReadFile(vid);
		}

		Check(video && RowHasToggle(video),
				"the video settings page shows the startup-splash row");
		free(video);
	}

	Check(OM_TitleFadeAlpha(0, life, 0) == 255, "a new title is opaque");
	start = life - OM_TITLE_FADE_MS;
	Check(OM_TitleFadeAlpha(start, life, 0) == 255, "the fade has not started yet");
	Check(OM_TitleFadeAlpha(life, life, 0) == 0, "the title is gone at the end of its life");
	Check(OM_TitleFadeAlpha(life + 100, life, 0) == 0, "past the end the title stays gone");

	prev = 255;
	mid = 0;
	biggest = 0;

	for (t = start; t <= life; t += 50)
	{
		int a = OM_TitleFadeAlpha(t, life, 0);
		int drop = prev - a;

		Check(a <= prev, "title alpha does not jump back up");
		Check(a >= 0 && a <= 255, "title alpha stays in range");

		if (drop > biggest)
		{
			biggest = drop;
		}

		if (a > 0 && a < 255)
		{
			mid = 1;
		}

		prev = a;
	}

	Check(mid == 1, "the fade passes through a partial alpha");
	Check(biggest < 128, "no sample step goes from opaque to gone");
	Check(OM_TitleFadeAlpha(life, life, 0) == 0, "the last sample is fully evaporated");

	/* A short title still evaporates instead of cutting. */
	prev = OM_TitleFadeAlpha(0, 400, 0);
	mid = 0;

	for (t = 40; t <= 400; t += 40)
	{
		int a = OM_TitleFadeAlpha(t, 400, 0);

		Check(a <= prev, "a short title fades down");

		if (a > 0 && a < 255)
		{
			mid = 1;
		}

		prev = a;
	}

	Check(mid == 1, "a short title has a partial alpha before it is gone");
	Check(OM_TitleFadeAlpha(400, 400, 0) == 0, "a short title ends at zero");

	spawn = 3.0f;
	shifted = OM_CineSpawnTime(spawn, 0.0f, 10.0f, 1);
	Check(shifted == spawn - 0.5f, "a spawn inside the opening cinematic is 0.5 s earlier");

	outside = OM_CineSpawnTime(12.0f, 0.0f, 10.0f, 1);
	Check(outside == 12.0f, "a spawn after the reel keeps its time");

	later = OM_CineSpawnTime(3.0f, 0.0f, 10.0f, 0);
	Check(later == 3.0f, "a spawn outside the opening cinematic keeps its time");

	Check(OM_CineSpawnTime(0.0f, 0.0f, 10.0f, 1) == -0.5f,
			"a spawn at the reel's first instant is 0.5 s earlier");
	Check(OM_CineSpawnTime(10.0f, 0.0f, 10.0f, 1) == 10.0f,
			"a spawn at the reel's end is not inside it");

	printf("menu_fade_spawn_test failures=%d\n", failed);
	return failed;
}
