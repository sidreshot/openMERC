/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc server: string packages on the server side - registration for
 * game import #47 (load strip/<name>.sp, announce it in a configstring),
 * the texts for #50/#51 and the argument packing of the string-package
 * prints #48/#49 (svc 34/36/37/39).
 *
 * The packages themselves are the engine core's (common/sof_strings.c).
 *
 * Implements: SPEC-001 (#47-#51), SPEC-201 s34-s39, SPEC-313 s1, SPEC-131
 */

#include "header/server.h"
#include "../common/header/sof/strings.h"

/* game import #47: load with the server owner bit, announce in 1464.. */
void
SV_SP_Register(const char *package)
{
	if (!package || !package[0])
	{
		return;
	}

	SV_StringPackageIndex(package);
	SP_Register(package, SP_OWNER_SERVER);
}

/* the server's packages go when the server shuts down or a new map starts */
void
SV_SP_Release(void)
{
	SP_Unregister(SP_OWNER_SERVER);
}

void
SV_SP_Init(void)
{
}

void
SV_SP_Shutdown(void)
{
	SV_SP_Release();
}

/* text of a string id. SPEC-001 has an undefined string drop the level; OpenMerc
   deviates (docs/DEVIATIONS.md): localized installs (e.g. the Russian GOG strip
   files) lack some ids the game prints, and dropping the level in the middle of
   the campaign for a missing pickup message is worse than printing nothing. The
   id is reported once and an empty text is used. */
static const char *
SV_SP_Lookup(int id, qboolean fatal)
{
	static byte warned[0x10000 / 8];
	const char *t;
	int i = id & 0xFFFF;

	if (!fatal)
	{
		return SP_GetStringByID(i);
	}

	t = SP_GetStringByID(i); /* drops for an unregistered package */

	if (!t || !t[0])
	{
		if (!(warned[i >> 3] & (1 << (i & 7))))
		{
			warned[i >> 3] |= (byte)(1 << (i & 7));
			Com_Printf("WARNING: String ID %04x not defined (printed as empty text)\n", i);
		}

		return "";
	}

	return t;
}

const char *
SV_SP_GetText(int id)
{
	const char *t = SV_SP_Lookup(id, false);

	return t ? t : "";
}

/* the player entity number for a %p argument: an entity pointer or a number */
static int
SV_SP_PlayerArg(intptr_t v)
{
	if (ge && ge->edicts && ge->edict_size &&
		((byte *)v >= (byte *)ge->edicts) &&
		((byte *)v < (byte *)ge->edicts + ge->edict_size * ge->max_edicts))
	{
		return NUM_FOR_EDICT((edict_t *)v);
	}

	return (int)v;
}

/* walks the conversions of a package text: %d 4 bytes, %hd/%hu 2, %p 1
   (player entity number), %s the string with its NUL, others nothing
   (SPEC-201 s34) */
int
SV_SP_PackArgs(int id, va_list args, byte *out, int capacity)
{
	const char *f = SV_SP_Lookup(id, true);
	int n = 0;

	for ( ; f && *f; f++)
	{
		qboolean half = false;

		if (*f != '%')
		{
			continue;
		}

		f++;

		if (*f == '%')
		{
			continue;
		}

		/* flags, width, precision */
		while (*f && strchr("-+ #0123456789.", *f))
		{
			f++;
		}

		if (*f == 'h')
		{
			half = true;
			f++;
		}
		else if (*f == 'l')
		{
			f++;
		}

		switch (*f)
		{
			case 'd':
			case 'i':
			case 'u':
			case 'x':
			case 'X':
			case 'c':
			{
				int v = va_arg(args, int);

				if (half)
				{
					if (n + 2 <= capacity)
					{
						out[n++] = v & 0xFF;
						out[n++] = (v >> 8) & 0xFF;
					}
				}
				else if (n + 4 <= capacity)
				{
					out[n++] = v & 0xFF;
					out[n++] = (v >> 8) & 0xFF;
					out[n++] = (v >> 16) & 0xFF;
					out[n++] = (v >> 24) & 0xFF;
				}

				break;
			}

			case 'p':
			{
				intptr_t v = va_arg(args, intptr_t);

				if (n + 1 <= capacity)
				{
					out[n++] = (byte)SV_SP_PlayerArg(v);
				}

				break;
			}

			case 's':
			{
				const char *s = va_arg(args, const char *);
				int l;

				if (!s)
				{
					s = "";
				}

				l = (int)strlen(s) + 1;

				if (n + l <= capacity)
				{
					memcpy(out + n, s, l);
					n += l;
				}

				break;
			}

			case 0:
				return n;

			default:
				break; /* other conversions consume nothing */
		}
	}

	return n;
}

/* #50: the text formatted locally; %p becomes the player's name */
int
SV_SP_Format(char *buf, int capacity, int id, va_list args)
{
	const char *f = SV_SP_Lookup(id, true);
	int n = 0;

	if (capacity <= 0)
	{
		return 0;
	}

	for ( ; f && *f && (n < capacity - 1); f++)
	{
		char spec[16], piece[1024];
		int s = 0;
		qboolean half = false;

		if (*f != '%')
		{
			buf[n++] = *f;
			continue;
		}

		spec[s++] = *f++;

		if (*f == '%')
		{
			buf[n++] = '%';
			continue;
		}

		while (*f && strchr("-+ #0123456789.", *f) && (s < 12))
		{
			spec[s++] = *f++;
		}

		if (*f == 'h')
		{
			half = true;
			f++;
		}
		else if (*f == 'l')
		{
			f++;
		}

		piece[0] = 0;

		switch (*f)
		{
			case 'd': case 'i': case 'u': case 'x': case 'X': case 'c':
			{
				int v = va_arg(args, int);

				if (half)
				{
					v = (*f == 'u') ? (int)(unsigned short)v : (int)(short)v;
				}

				spec[s++] = *f;
				spec[s] = 0;
				Com_sprintf(piece, sizeof(piece), spec, v);
				break;
			}

			case 'p':
			{
				int e = SV_SP_PlayerArg(va_arg(args, intptr_t));

				if ((e >= 1) && (e <= svs.numclients))
				{
					Q_strlcpy(piece, svs.clients[e - 1].name, sizeof(piece));
				}
				else
				{
					Q_strlcpy(piece, "!@#$%^&*()", sizeof(piece));
				}

				break;
			}

			case 's':
			{
				const char *str = va_arg(args, const char *);

				spec[s++] = 's';
				spec[s] = 0;
				Com_sprintf(piece, sizeof(piece), spec, str ? str : "");
				break;
			}

			case 0:
				f--;
				break;

			default:
				break;
		}

		{
			int l = (int)strlen(piece);

			if (l > capacity - 1 - n)
			{
				l = capacity - 1 - n;
			}

			memcpy(buf + n, piece, l);
			n += l;
		}
	}

	buf[n] = 0;
	return n;
}

static void
SV_SP_Write(sizebuf_t *msg, int id, const byte *data, int len, qboolean obituary)
{
	if (obituary)
	{
		MSG_WriteByte(msg, sofsvc_sp_print_obit);
		MSG_WriteShort(msg, id);
		MSG_WriteByte(msg, len);
		SZ_Write(msg, (void *)data, len);
	}
	else if (!len)
	{
		MSG_WriteByte(msg, sofsvc_sp_print);
		MSG_WriteShort(msg, id);
	}
	else if (len <= 255)
	{
		MSG_WriteByte(msg, sofsvc_sp_print_data_1);
		MSG_WriteShort(msg, id);
		MSG_WriteByte(msg, len);
		SZ_Write(msg, (void *)data, len);
	}
	else
	{
		MSG_WriteByte(msg, sofsvc_sp_print_data_2);
		MSG_WriteShort(msg, id);
		MSG_WriteShort(msg, len);
		SZ_Write(msg, (void *)data, len);
	}
}

/* #48/#49: to one client, or (NULL) every client in the game */
void
SV_SP_Print(const edict_t *ent, int id, va_list args, qboolean obituary)
{
	byte data[1024];
	int len, i;
	client_t *cl;

	len = SV_SP_PackArgs(id, args, data, obituary ? 255 : (int)sizeof(data));

	if (ent)
	{
		i = NUM_FOR_EDICT(ent);

		if ((i < 1) || (i > svs.numclients))
		{
			return;
		}

		cl = svs.clients + (i - 1);

		if (!SV_ClientIsBot(cl))
		{
			SV_SP_Write(&cl->netchan.message, id, data, len, obituary);
		}

		return;
	}

	for (i = 0, cl = svs.clients; i < svs.numclients; i++, cl++)
	{
		if (cl->state == cs_spawned)
		{
			SV_SP_Write(&cl->netchan.message, id, data, len, obituary);
		}
	}
}

/* an argument-less package print from the engine itself */
void
SV_SP_PrintClient(client_t *cl, int id)
{
	if (!SV_ClientIsBot(cl))
	{
		SV_SP_Write(&cl->netchan.message, id, NULL, 0, false);
	}
}
