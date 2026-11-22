/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Shipped graphics-config loader, flare light radius, and the mode B docs.
 * Exit code = number of failed checks.
 *
 * Usage: config_flare_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "header/om_fxcfg.h"

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

static int
Near(float a, float b)
{
	float d = a - b;

	if (d < 0.0f)
	{
		d = -d;
	}

	return d <= 0.001f;
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

	if (n < 0 || n > 2000000)
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

/* Walk up until the shipped config and the newcomer docs are both visible. */
static int
FindRoot(char *dir, int dirn)
{
	int up;

	if (!getcwd(dir, dirn))
	{
		return 0;
	}

	for (up = 0; up < 10; up++)
	{
		char cfg[768];
		char doc[768];
		size_t n;

		snprintf(cfg, sizeof(cfg), "%s/data/base/om_gfx.cfg", dir);
		snprintf(doc, sizeof(doc), "%s/docs/BUILD-linux.md", dir);

		if (Exists(cfg) && Exists(doc))
		{
			return 1;
		}

		n = strlen(dir);

		if (n + 4 >= (size_t)dirn)
		{
			return 0;
		}

		memcpy(dir + n, "/..", 4);
	}

	return 0;
}

static void
CheckKey(const char *key)
{
	char what[80];

	snprintf(what, sizeof(what), "default %s", key);
	Check(Near(OM_FxCfg_Live(key), OM_FxCfg_Default(key)), what);
}

static void
CheckDefaults(void)
{
	OM_FxCfg_UseText("");
	CheckKey("fog_strength");
	CheckKey("fog_sib");
	CheckKey("snow_sib1");
	CheckKey("snow_cap");
	CheckKey("tess_scale");
	CheckKey("tess_weapon");
	CheckKey("tess_models");
	CheckKey("tess_round");
	CheckKey("hdr_gamma");
	CheckKey("hdr_shoulder");
	Check(Near(OM_FxCfg_Default("fog_strength"), 1.1f), "fog strength default 1.1");
	Check(Near(OM_FxCfg_Default("fog_sib"), 1.15f), "sib fog default 1.15");
	Check(Near(OM_FxCfg_Default("snow_sib1"), 1.25f), "sib1 snow default 1.25");
	Check(Near(OM_FxCfg_Default("snow_cap"), 3.0f), "snow cap default 3");
	Check(Near(OM_FxCfg_Default("tess_scale"), 8.0f), "tess scale default 8");
	Check(Near(OM_FxCfg_Default("tess_weapon"), 1.0f), "tess weapon default 1");
	Check(Near(OM_FxCfg_Default("tess_models"), 3.0f), "tess models default 3");
	Check(Near(OM_FxCfg_Default("tess_round"), 1.0f), "tess round default 1");
	Check(Near(OM_FxCfg_Default("hdr_gamma"), 2.2f), "hdr gamma default 2.2");
	Check(Near(OM_FxCfg_Default("hdr_shoulder"), 0.6f), "hdr shoulder default 0.6");
	Check(Near(OM_FxCfg_FogMul("maps/sib1.bsp"), 1.0f), "default fog mul is 1 on sib1");
	Check(Near(OM_FxCfg_FogMul("maps/trn1.bsp"), 1.0f), "default fog mul is 1 off sib");
	Check(Near(OM_FxCfg_SnowMul("maps/sib1.bsp"), 1.0f), "default snow mul is 1 on sib1");
	Check(Near(OM_FxCfg_SnowMul("maps/sib2.bsp"), 1.0f), "snow sib1 scale stays off sib2");
	OM_FxCfg_UseText(NULL);
	Check(Near(OM_FxCfg_Live("hdr_gamma"), 2.2f), "null text keeps hdr gamma");
}

static void
CheckFileValues(void)
{
	static const char *text =
		"# sample\n"
		"[fog]\n"
		"strength = 2.2\n"
		"sib = 2.3\n"
		"\n"
		"[snow]\n"
		"sib1 = 2.5\n"
		"cap = 4\n"
		"\n"
		"[tess]\n"
		"scale = 6\n"
		"weapon = 9\n"
		"models = 2\n"
		"round = 0\n"
		"\n"
		"[hdr]\n"
		"gamma = 1.8\n"
		"shoulder = 0.4\n"
		"; ignored\n";
	om_fxkv_t kv[OM_FXCFG_MAX];
	int n = OM_FxCfg_Parse(text, kv, OM_FXCFG_MAX);

	Check(n >= 10, "sample stores the named keys");
	Check(Near(OM_FxCfg_Number(kv, n, "fog_strength"), 2.2f), "file fog strength");
	Check(Near(OM_FxCfg_Number(kv, n, "fog_sib"), 2.3f), "file sib fog");
	Check(Near(OM_FxCfg_Number(kv, n, "snow_sib1"), 2.5f), "file snow density");
	Check(Near(OM_FxCfg_Number(kv, n, "snow_cap"), 4.0f), "file snow cap");
	Check(Near(OM_FxCfg_Number(kv, n, "tess_scale"), 6.0f), "file tess scale");
	Check(Near(OM_FxCfg_Number(kv, n, "tess_weapon"), 9.0f), "file tess weapon");
	Check(Near(OM_FxCfg_Number(kv, n, "tess_models"), 2.0f), "file tess models");
	Check(Near(OM_FxCfg_Number(kv, n, "tess_round"), 0.0f), "file tess round zero is kept");
	Check(Near(OM_FxCfg_Number(kv, n, "hdr_gamma"), 1.8f), "file hdr gamma");
	Check(Near(OM_FxCfg_Number(kv, n, "hdr_shoulder"), 0.4f), "file hdr shoulder");
	Check(Near(OM_FxCfg_Number(kv, n, "not_a_key"), 0.0f), "unknown key is 0");

	OM_FxCfg_UseText(text);
	Check(Near(OM_FxCfg_Live("fog_strength"), 2.2f), "live fog strength");
	Check(Near(OM_FxCfg_Live("tess_scale"), 6.0f), "live tess scale");
	Check(Near(OM_FxCfg_Live("hdr_gamma"), 1.8f), "live hdr gamma");
	Check(Near(OM_FxCfg_Live("snow_sib1"), 2.5f), "live snow density");
	Check(Near(OM_FxCfg_FogMul("maps/trn1.bsp"), 2.2f / 1.1f), "fog mul follows the file");
	Check(Near(OM_FxCfg_FogMul("maps/sib2.bsp"), (2.2f / 1.1f) * (2.3f / 1.15f)), "sib fog mul stacks");
	Check(Near(OM_FxCfg_FogMul("maps/sib10.bsp"), 2.2f / 1.1f), "sib10 is not sib1");
	Check(Near(OM_FxCfg_SnowMul("maps/sib1.bsp"), 2.5f / 1.25f), "snow mul follows the file");
	Check(Near(OM_FxCfg_SnowMul("maps/sib2.bsp"), 1.0f), "snow file does not retune sib2");

	OM_FxCfg_UseText("[fog]\nstrength = 1.1\n");
	Check(Near(OM_FxCfg_Live("fog_strength"), 1.1f), "partial file fog");
	Check(Near(OM_FxCfg_Live("hdr_shoulder"), 0.6f), "missing hdr key keeps 0.6");
	Check(Near(OM_FxCfg_Live("snow_cap"), 3.0f), "missing snow cap keeps 3");
	Check(Near(OM_FxCfg_FogMul("maps/sib3.bsp"), 1.0f), "partial file fog mul stays 1");
}

static void
CheckShippedFile(const char *root)
{
	char path[768];
	char *text;

	snprintf(path, sizeof(path), "%s/data/base/om_gfx.cfg", root);
	text = ReadFile(path);
	Check(text != NULL, "shipped om_gfx.cfg opens");

	if (!text)
	{
		return;
	}

	OM_FxCfg_UseText(text);
	Check(Near(OM_FxCfg_Live("fog_strength"), 1.1f), "shipped fog strength");
	Check(Near(OM_FxCfg_Live("fog_sib"), 1.15f), "shipped sib fog");
	Check(Near(OM_FxCfg_Live("snow_sib1"), 1.25f), "shipped snow density");
	Check(Near(OM_FxCfg_Live("snow_cap"), 3.0f), "shipped snow cap");
	Check(Near(OM_FxCfg_Live("tess_scale"), 8.0f), "shipped tess scale");
	Check(Near(OM_FxCfg_Live("tess_weapon"), 1.0f), "shipped tess weapon");
	Check(Near(OM_FxCfg_Live("tess_models"), 3.0f), "shipped tess models");
	Check(Near(OM_FxCfg_Live("tess_round"), 1.0f), "shipped tess round");
	Check(Near(OM_FxCfg_Live("hdr_gamma"), 2.2f), "shipped hdr gamma");
	Check(Near(OM_FxCfg_Live("hdr_shoulder"), 0.6f), "shipped hdr shoulder");
	Check(Near(OM_FxCfg_FogMul("sib1"), 1.0f), "shipped file does not change fog");
	Check(Near(OM_FxCfg_SnowMul("sib1"), 1.0f), "shipped file does not change snow");
	free(text);
}

static void
CheckFlare(void)
{
	float road = OM_FxLightRadius(0.0f, OM_FxGlowClass("textures/sprites/flare4"), 6.0f);
	float blue = OM_FxLightRadius(0.0f, OM_FxGlowClass("textures/sprites/blueflare"), 8.0f);

	Check(OM_FxGlowClass("textures/sprites/flare4") == 1, "flare4 is a flare");
	Check(OM_FxGlowClass("textures/sprites/FLARE4") == 1, "flare class ignores case");
	Check(OM_FxGlowClass("textures/sprites/blueflare") == 1, "blue flare is the same class");
	Check(OM_FxGlowClass("textures/sprites/gb_smoke01") == 0, "smoke is not a flare");
	Check(OM_FxGlowClass("textures/sprites/sparkpart") == 0, "sparks are not a flare");
	Check(OM_FxGlowClass("textures/sprites/gb_fire02") == 0, "fire graphic is not a flare");
	Check(OM_FxGlowClass(NULL) == 0, "null graphic is dark");
	Check(road > 64.0f, "sib1 flare radius clears the 64 cutoff");
	Check(blue > 64.0f, "second flare on the same path lights");
	Check(road == blue, "both flare sprites share the radius");
	Check(OM_FxLightRadius(120.0f, 1, 6.0f) == 120.0f, "file radius 120 stays 120");
	Check(OM_FxLightRadius(30.0f, 0, 0.0f) == 30.0f, "file radius 30 is not boosted");
	Check(OM_FxLightRadius(0.0f, OM_FxGlowClass("textures/sprites/gb_smoke01"), 8.0f) == 0.0f,
			"smoke stays dark");
	Check(OM_FxLightRadius(0.0f, 1, 1.0f) == 0.0f, "size 1 stays dark");
	Check(OM_FxLightRadius(0.0f, 1, 3.9f) == 0.0f, "size under 4 stays dark");
	Check(Near(OM_FxLightLifeMs(0.3f), 300.0f), "0.3s life stays 300 ms");
	Check(OM_FxLightLifeMs(0.01f) == 50.0f, "sub-frame life is raised");
	Check(OM_FxLightLifeMs(0.0f) == 0.0f, "zero life stays 0");
	Check(OM_FxLightLifeMs(-2.0f) == 0.0f, "negative life stays 0");
	Check(OM_FxKeepRising(1.0f, 3, 0.01f, 1) == 1, "young style 3 is kept");
	Check(OM_FxKeepRising(1.0f, 3, 0.5f, 1) == 0, "old low alpha is not kept");
	Check(OM_FxKeepRising(255.0f, 0, 1.0f, 1) == 1, "visible alpha is kept");
	Check(OM_FxKeepRising(1.0f, 3, 0.01f, 0) == 0, "a dead particle is not kept");
}

static void
CheckWired(const char *root)
{
	char path[768];
	char *emitter;
	char *system;
	char *particle;

	snprintf(path, sizeof(path), "%s/engine/src/client/fx/fx_emitter.c", root);
	emitter = ReadFile(path);
	snprintf(path, sizeof(path), "%s/engine/src/client/fx/fx_system.c", root);
	system = ReadFile(path);
	snprintf(path, sizeof(path), "%s/engine/src/client/fx/fx_particle.c", root);
	particle = ReadFile(path);
	Check(emitter && strstr(emitter, "OM_FxLightRadius") && strstr(emitter, "OM_FxGlowClass")
			&& strstr(emitter, "CL_SoF_AddLight"),
			"emitter registers a flare light");
	Check(system && strstr(system, "OM_FxLightRadius") && strstr(system, "OM_FxLightLifeMs"),
			"light primitive keeps radius and life");
	Check(particle && strstr(particle, "OM_FxKeepRising"), "young flare particle is kept");
	free(emitter);
	free(system);
	free(particle);
}

static void
CheckDocs(const char *root)
{
	static const char *files[] = {
		"README.md",
		"dist/win/README.txt",
		"docs/KNOWN_ISSUES.md",
		"docs/BUILD-linux.md",
		"docs/BUILD-macos.md",
		"docs/impl/INSTALL-LAYOUT.md",
		"tests/X1/inttest/README-TEST.txt",
	};
	static const char *banned[] = {
		"rename openmerc\\gamex86.dll",
		"rename openmerc/gamex86.dll",
		"переименуйте openmerc",
		"is chosen by removing or renaming",
		"Rename them back",
		"the most complete campaign today",
		"сейчас самая полная кампания",
		"For mode A delete",
		"режим A (по умолчанию)",
	};
	int i;

	for (i = 0; i < (int)(sizeof(files) / sizeof(files[0])); i++)
	{
		char path[768];
		char what[160];
		char *text;
		int b;

		snprintf(path, sizeof(path), "%s/%s", root, files[i]);
		text = ReadFile(path);
		snprintf(what, sizeof(what), "%s names mode B only", files[i]);
		Check(text && strstr(text, "Only mode B is built and maintained."), what);

		if (text)
		{
			for (b = 0; b < (int)(sizeof(banned) / sizeof(banned[0])); b++)
			{
				snprintf(what, sizeof(what), "%s does not say to finish mode A (%d)", files[i], b);
				Check(strstr(text, banned[b]) == NULL, what);
			}

			free(text);
		}
	}
}

int
main(void)
{
	char root[640];

	CheckDefaults();
	CheckFileValues();
	CheckFlare();

	if (!FindRoot(root, (int)sizeof(root)))
	{
		Check(0, "repository root found");
	}
	else
	{
		CheckShippedFile(root);
		CheckWired(root);
		CheckDocs(root);
	}

	printf("config_flare_test failures=%d\n", failed);
	return failed;
}
