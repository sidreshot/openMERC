/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * See header/om_fxcfg.h.
 */

#include <stdlib.h>
#include <string.h>

#include "header/om_fxcfg.h"

static om_fxkv_t g_kv[OM_FXCFG_MAX];
static int g_n;

static void
Trim(char *s)
{
	char *a = s;
	char *b;

	while (*a == ' ' || *a == '\t' || *a == '\r')
	{
		a++;
	}

	if (a != s)
	{
		memmove(s, a, strlen(a) + 1);
	}

	b = s + strlen(s);

	while (b > s && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r'))
	{
		b--;
	}

	*b = 0;
}

static void
CutComment(char *s)
{
	char *p;

	for (p = s; *p; p++)
	{
		if (*p == '#' || *p == ';')
		{
			*p = 0;
			return;
		}
	}
}

static void
Lower(char *s)
{
	for (; *s; s++)
	{
		if (*s >= 'A' && *s <= 'Z')
		{
			*s = (char)(*s - 'A' + 'a');
		}
	}
}

static int
KeyOk(const char *s)
{
	if (!s || !s[0])
	{
		return 0;
	}

	for (; *s; s++)
	{
		char c = *s;

		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
		{
			return 0;
		}
	}

	return 1;
}

static int
Store(om_fxkv_t *kv, int n, int maxn, const char *key, float value)
{
	int i;

	for (i = 0; i < n; i++)
	{
		if (kv[i].set && strcmp(kv[i].key, key) == 0)
		{
			kv[i].value = value;
			return n;
		}
	}

	if (n >= maxn || strlen(key) >= sizeof(kv[0].key))
	{
		return n;
	}

	memset(kv[n].key, 0, sizeof(kv[n].key));
	memcpy(kv[n].key, key, strlen(key));
	kv[n].value = value;
	kv[n].set = 1;
	return n + 1;
}

static void
ParseLine(char *line, char *section, int sectn, om_fxkv_t *kv, int *n, int maxn)
{
	char *eq;
	char name[32];
	char *end;
	float v;
	int wr;

	CutComment(line);
	Trim(line);

	if (!line[0])
	{
		return;
	}

	if (line[0] == '[')
	{
		char *rb = strchr(line, ']');

		section[0] = 0;

		if (!rb)
		{
			return;
		}

		*rb = 0;
		Trim(line + 1);
		Lower(line + 1);

		if (!KeyOk(line + 1) || (int)strlen(line + 1) >= sectn)
		{
			return;
		}

		memcpy(section, line + 1, strlen(line + 1) + 1);
		return;
	}

	eq = strchr(line, '=');

	if (!eq)
	{
		return;
	}

	*eq = 0;
	Trim(line);
	Trim(eq + 1);
	Lower(line);

	if (!KeyOk(line))
	{
		return;
	}

	end = NULL;
	v = strtof(eq + 1, &end);

	if (end == eq + 1)
	{
		return;
	}

	if (section[0])
	{
		wr = (int)strlen(section) + 1 + (int)strlen(line);

		if (wr >= (int)sizeof(name))
		{
			return;
		}

		memcpy(name, section, strlen(section));
		name[strlen(section)] = '_';
		memcpy(name + strlen(section) + 1, line, strlen(line) + 1);
	}
	else
	{
		if (strlen(line) >= sizeof(name))
		{
			return;
		}

		memcpy(name, line, strlen(line) + 1);
	}

	*n = Store(kv, *n, maxn, name, v);
}

int
OM_FxCfg_Parse(const char *text, om_fxkv_t *kv, int maxn)
{
	char *buf;
	char *p;
	char section[24];
	size_t bytes;
	int n = 0;

	if (!kv || maxn <= 0)
	{
		return 0;
	}

	memset(kv, 0, sizeof(*kv) * (size_t)maxn);
	section[0] = 0;

	if (!text || !text[0])
	{
		return 0;
	}

	bytes = strlen(text);
	buf = (char *)malloc(bytes + 1);

	if (!buf)
	{
		return 0;
	}

	memcpy(buf, text, bytes + 1);

	for (p = buf; *p; )
	{
		char *eol = strchr(p, '\n');

		if (eol)
		{
			*eol = 0;
		}

		ParseLine(p, section, (int)sizeof(section), kv, &n, maxn);

		if (!eol)
		{
			break;
		}

		p = eol + 1;
	}

	free(buf);
	return n;
}

float
OM_FxCfg_Default(const char *key)
{
	if (!key)
	{
		return 0.0f;
	}

	if (!strcmp(key, "fog_strength"))
	{
		return 1.1f;
	}

	if (!strcmp(key, "fog_sib"))
	{
		return 1.15f;
	}

	if (!strcmp(key, "snow_sib1"))
	{
		return 1.25f;
	}

	if (!strcmp(key, "snow_cap"))
	{
		return 3.0f;
	}

	if (!strcmp(key, "tess_scale"))
	{
		return 8.0f;
	}

	if (!strcmp(key, "tess_weapon"))
	{
		return 1.0f;
	}

	if (!strcmp(key, "tess_models"))
	{
		return 3.0f;
	}

	if (!strcmp(key, "tess_round"))
	{
		return 1.0f;
	}

	if (!strcmp(key, "hdr_gamma"))
	{
		return 2.2f;
	}

	if (!strcmp(key, "hdr_shoulder"))
	{
		return 0.6f;
	}

	return 0.0f;
}

float
OM_FxCfg_Get(const om_fxkv_t *kv, int n, const char *key, float fallback)
{
	int i;

	if (!kv || !key)
	{
		return fallback;
	}

	for (i = 0; i < n; i++)
	{
		if (kv[i].set && !strcmp(kv[i].key, key))
		{
			return kv[i].value;
		}
	}

	return fallback;
}

float
OM_FxCfg_Number(const om_fxkv_t *kv, int n, const char *key)
{
	return OM_FxCfg_Get(kv, n, key, OM_FxCfg_Default(key));
}

void
OM_FxCfg_UseText(const char *text)
{
	g_n = OM_FxCfg_Parse(text ? text : "", g_kv, OM_FXCFG_MAX);
}

float
OM_FxCfg_Live(const char *key)
{
	return OM_FxCfg_Number(g_kv, g_n, key);
}

static int
FoldEq(char a, char b)
{
	if (a >= 'A' && a <= 'Z')
	{
		a = (char)(a - 'A' + 'a');
	}

	if (b >= 'A' && b <= 'Z')
	{
		b = (char)(b - 'A' + 'a');
	}

	return a == b;
}

static int
Alnum(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static int
HasWord(const char *text, const char *word)
{
	size_t n;
	size_t i;
	size_t at;

	if (!text || !word || !word[0])
	{
		return 0;
	}

	n = strlen(word);

	for (at = 0; text[at]; at++)
	{
		for (i = 0; i < n; i++)
		{
			if (!text[at + i] || !FoldEq(text[at + i], word[i]))
			{
				break;
			}
		}

		if (i != n)
		{
			continue;
		}

		if (at > 0 && Alnum(text[at - 1]))
		{
			continue;
		}

		if (Alnum(text[at + n]))
		{
			continue;
		}

		return 1;
	}

	return 0;
}

static int
SibMap(const char *mapname)
{
	return HasWord(mapname, "sib1") || HasWord(mapname, "sib2") || HasWord(mapname, "sib3");
}

float
OM_FxCfg_FogMul(const char *mapname)
{
	float base = OM_FxCfg_Default("fog_strength");
	float k;

	if (base == 0.0f)
	{
		return 1.0f;
	}

	k = OM_FxCfg_Live("fog_strength") / base;

	if (SibMap(mapname))
	{
		float sib = OM_FxCfg_Default("fog_sib");

		if (sib != 0.0f)
		{
			k *= OM_FxCfg_Live("fog_sib") / sib;
		}
	}

	if (k < 0.0f)
	{
		k = 0.0f;
	}

	return k;
}

float
OM_FxCfg_SnowMul(const char *mapname)
{
	float base;
	float k;

	if (!HasWord(mapname, "sib1"))
	{
		return 1.0f;
	}

	base = OM_FxCfg_Default("snow_sib1");

	if (base == 0.0f)
	{
		return 1.0f;
	}

	k = OM_FxCfg_Live("snow_sib1") / base;

	if (k < 0.0f)
	{
		k = 0.0f;
	}

	return k;
}

int
OM_FxGlowClass(const char *graphic)
{
	static const char flare[] = "flare";
	size_t i;
	size_t n;

	if (!graphic)
	{
		return 0;
	}

	n = sizeof(flare) - 1;

	for (i = 0; graphic[i]; i++)
	{
		size_t k;

		for (k = 0; k < n; k++)
		{
			if (!graphic[i + k] || !FoldEq(graphic[i + k], flare[k]))
			{
				break;
			}
		}

		if (k == n)
		{
			return 1;
		}
	}

	return 0;
}

float
OM_FxLightRadius(float file_radius, int glow, float size)
{
	if (file_radius > 0.0f)
	{
		return file_radius;
	}

	/* 160 clears the shader cutoff of 64, so about 96 units of the light remain. */
	if (glow && size >= 4.0f)
	{
		return 160.0f;
	}

	return 0.0f;
}

float
OM_FxLightLifeMs(float life_s)
{
	if (!(life_s > 0.0f))
	{
		return 0.0f;
	}

	if (life_s < (1.0f / 30.0f))
	{
		return 50.0f;
	}

	return life_s * 1000.0f;
}

int
OM_FxKeepRising(float alpha, int astyle, float age_s, int alive)
{
	if (!alive)
	{
		return 0;
	}

	if (alpha >= 2.0f)
	{
		return 1;
	}

	if (astyle >= 2 && age_s >= 0.0f && age_s < (1.0f / 30.0f))
	{
		return 1;
	}

	return 0;
}
