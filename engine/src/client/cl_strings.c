/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Soldier of Fortune 1.07f string packages on the client: the client-owned
 * packages (configstrings 1464..1492), lookups for the HUD and menus, and
 * the string-package prints of the server (svc_sp_print and its data /
 * obituary forms). Loading and storage are the engine's runtime
 * (common/sof_strings.c, I1a); this file decides when the client registers
 * and releases its packages and presents the prints.
 *
 * Implements: SPEC-313 section 1 ("Package owners", client bit),
 *             SPEC-201 section 34, SPEC-310 section 6
 */

#include "header/client.h"
#include "header/sofclient.h"
#include "../common/header/sof/strings.h"

/* package IDs known to be loaded (the runtime drops the level on a lookup
   in a package that is not loaded; the HUD and the prints must not) */
static qboolean known[SP_MAX_PACKAGES];
static qboolean engine_known;

/* configstring names already registered this level (1464..1492) */
#define SP_CS_FIRST 1464
#define SP_CS_LAST 1492
static char cs_done[SP_CS_LAST - SP_CS_FIRST + 1][64];

static void
SP_KnowEngine(void)
{
	static const char *names[] = {"items", "weapons", "general", "dm_generic"};
	int i, id;

	if (engine_known)
	{
		return;
	}

	engine_known = true;

	/* already loaded at start (frame.c); this only returns their IDs */
	for (i = 0; i < 4; i++)
	{
		id = SP_Register(names[i], SP_OWNER_ENGINE);

		if (id >= 0)
		{
			known[id & 0xFF] = true;
		}
	}
}

/* packages named by configstrings 1464..1492 (client owner bit) */
static void
SP_RegisterLevel(void)
{
	int i, id;

	if (!CL_SoF_Active())
	{
		return;
	}

	for (i = SP_CS_FIRST; i <= SP_CS_LAST; i++)
	{
		const char *n = CL_SoF_ConfigString(i);
		char *done = cs_done[i - SP_CS_FIRST];

		if (!n[0] || !strcmp(done, n))
		{
			continue;
		}

		Q_strlcpy(done, n, sizeof(cs_done[0]));
		id = SP_Register(n, SP_OWNER_CLIENT);

		if (id >= 0)
		{
			known[id & 0xFF] = true;
		}
	}
}

/* a new level: the client releases its packages (SPEC-313) */
void
CL_Strings_LevelChanged(void)
{
	int i;

	SP_Unregister(SP_OWNER_CLIENT);
	memset(cs_done, 0, sizeof(cs_done));

	for (i = 0; i < SP_MAX_PACKAGES; i++)
	{
		known[i] = false;
	}

	engine_known = false;
}

/* the level's media are registered: the client packages again */
void
CL_Strings_RegisterMedia(void)
{
	memset(cs_done, 0, sizeof(cs_done));
	SP_KnowEngine();
	SP_RegisterLevel();
}

static qboolean
SP_Known(int id)
{
	int pkg = (id >> 8) & 0xFF;

	SP_KnowEngine();

	if (!known[pkg])
	{
		SP_RegisterLevel();
	}

	return known[pkg];
}

/* ---------------------------------------------------------------------
 * lookups
 * --------------------------------------------------------------------- */

const char *
CL_StringById(int id)
{
	return SP_Known(id) ? SP_GetStringByID(id & 0xFFFF) : "";
}

int
CL_StringFlags(int id)
{
	return SP_Known(id) ? SP_GetFlagsByID(id & 0xFFFF) : 0;
}

const char *
CL_StringByName(const char *ref)
{
	if (!ref || !ref[0])
	{
		return "";
	}

	SP_KnowEngine();
	return SP_GetStringByName(ref);
}

/* ---------------------------------------------------------------------
 * string-package prints (SPEC-201 section 34)
 * --------------------------------------------------------------------- */

static const char *
SP_PlayerName(int entnum)
{
	static char name[64];
	int slot = entnum - 1;
	char *p;

	if ((slot < 0) || (slot >= MAX_CLIENTS))
	{
		return "!@#$%^&*()";
	}

	Q_strlcpy(name, CL_SoF_ConfigString(1415 + slot), sizeof(name));

	if (!name[0])
	{
		return "!@#$%^&*()";
	}

	p = strchr(name, '\\');

	if (p)
	{
		*p = 0;
	}

	return name;
}

static void
SP_Append(char *out, size_t size, size_t *o, const char *s)
{
	while (*s && (*o < size - 1))
	{
		out[(*o)++] = *s++;
	}

	out[*o] = 0;
}

/* formats the package text with the packed arguments; counts %p arguments
   and remembers the first two */
static void
SP_Format(const char *fmt, const byte *data, int len, char *out, size_t size,
		int *pcount, int pvals[2])
{
	size_t o = 0;
	int pos = 0;
	const char *f = fmt;

	*pcount = 0;
	pvals[0] = pvals[1] = 0;
	out[0] = 0;

	while (*f && (o < size - 1))
	{
		char spec[32], tmp[256];
		size_t sl = 0;
		int hcount = 0, lcount = 0;
		char conv;

		if (*f != '%')
		{
			out[o++] = *f++;
			out[o] = 0;
			continue;
		}

		/* conversion: flags, width, precision, length, type */
		spec[sl++] = *f++;

		while (*f && strchr("-+ #0", *f) && (sl < sizeof(spec) - 4))
		{
			spec[sl++] = *f++;
		}

		while (*f && (isdigit((unsigned char)*f) || (*f == '.')) && (sl < sizeof(spec) - 4))
		{
			spec[sl++] = *f++;
		}

		while (*f && strchr("hlL", *f))
		{
			if (*f == 'h')
			{
				hcount++;
			}
			else
			{
				lcount++;
			}

			f++;
		}

		conv = *f;

		if (!conv)
		{
			break;
		}

		f++;
		spec[sl] = 0;

		switch (conv)
		{
			case '%':
				out[o++] = '%';
				out[o] = 0;
				break;

			case 'd':
			case 'i':
			case 'u':
			case 'x':
			case 'X':
			case 'o':
			case 'c':
			{
				int v;

				if (hcount)
				{
					if (pos + 2 > len)
					{
						return;
					}

					v = data[pos] | (data[pos + 1] << 8);

					if ((conv == 'd') || (conv == 'i'))
					{
						v = (short)v;
					}

					pos += 2;
				}
				else
				{
					if (pos + 4 > len)
					{
						return;
					}

					v = (int)((unsigned)data[pos] | ((unsigned)data[pos + 1] << 8) |
							((unsigned)data[pos + 2] << 16) | ((unsigned)data[pos + 3] << 24));
					pos += 4;
				}

				spec[sl] = conv;
				spec[sl + 1] = 0;
				Com_sprintf(tmp, sizeof(tmp), spec, v);
				SP_Append(out, size, &o, tmp);
				break;
			}

			case 'p':
			{
				int e;

				if (pos + 1 > len)
				{
					return;
				}

				e = data[pos++];

				if (*pcount < 2)
				{
					pvals[*pcount] = e;
				}

				(*pcount)++;
				SP_Append(out, size, &o, SP_PlayerName(e));
				break;
			}

			case 's':
			{
				char str[1024];
				size_t n = 0;

				while ((pos < len) && data[pos] && (n < sizeof(str) - 1))
				{
					str[n++] = (char)data[pos++];
				}

				str[n] = 0;

				if (pos < len)
				{
					pos++; /* the terminating NUL */
				}

				spec[sl] = 's';
				spec[sl + 1] = 0;
				Com_sprintf(tmp, sizeof(tmp), spec, str);
				SP_Append(out, size, &o, tmp);
				break;
			}

			default:
				/* other conversions consume nothing */
				break;
		}

		(void)lcount;
	}
}

void
CL_SP_Print(int id, const byte *data, int len, qboolean obit)
{
	static const byte nodata[1];
	char out[1024];
	const char *text;
	int flags, pcount, pvals[2];

	if (!SP_Known(id))
	{
		return; /* unknown ids are ignored */
	}

	text = SP_GetStringByID(id & 0xFFFF);

	if (!data || (len < 0))
	{
		data = nodata;
		len = 0;
	}

	SP_Format(text, data, len, out, sizeof(out), &pcount, pvals);

	if (obit)
	{
		int filter = (int)Cvar_Get("cl_obit_filter", "0", 0)->value;
		int me = cl.playernum + 1;

		if (filter >= 2)
		{
			return;
		}

		if ((filter == 1) && (pcount > 0) && (pvals[0] != me) &&
			!((pcount > 1) && (pvals[1] == me)))
		{
			return;
		}
	}

	flags = SP_GetFlagsByID(id & 0xFFFF);

	/* the first matching flag wins */
	if (flags & SP_FLAG_CREDIT)
	{
		SCR_CreditPrint(out);
	}
	else if (flags & SP_FLAG_CAPTIONED)
	{
		if (Cvar_Get("cl_subtitles", "1", CVAR_ARCHIVE)->value || (flags & SP_FLAG_ALWAYS_PRINT))
		{
			SCR_CaptionPrint(out);
		}
	}
	else if (flags & SP_FLAG_CENTERED)
	{
		SCR_CenterPrint(out);
	}
	else if (flags & SP_FLAG_TYPEAMATIC)
	{
		SCR_TypewriterPrint(0, 104, 5, out);
	}
	else if (flags & SP_FLAG_LAYOUT)
	{
		HUD_LayoutText(out);
	}
	else
	{
		Com_Printf("%s", out);
	}
}
