/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc: .rmf menu documents - loading, tokeniser, string references,
 * includes, document-level tags, frames and page parsing.
 *
 * Implements: SPEC-130, SPEC-800 (sections 1-5, 8), SPEC-801 (frame pages)
 */

#include <stdarg.h>

#include "header/menu.h"
#include "header/m_draw.h"

/* parse-test counters (m_test.c) */
int mp_errorcount;
void (*mp_errorhook)(const char *tag, const char *msg);

/* ------------------------------------------------------------------ */
/* errors (SPEC-800 section 4)                                         */

void
MP_Error(mparse_t *p, const char *fmt, ...)
{
	va_list ap;
	char msg[1024];

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	msg[sizeof(msg) - 1] = 0;
	va_end(ap);

	mp_errorcount++;

	if (mp_errorhook)
	{
		mp_errorhook(p->tag, msg);
	}

	if (developer && developer->value)
	{
		Com_Printf("%d : Command \"%s\" : %s\n", p->line, p->tag, msg);
	}
}

/* ------------------------------------------------------------------ */
/* text buffer                                                         */

static void
MP_Reserve(mparse_t *p, int extra)
{
	if (p->len + extra + 1 > p->cap)
	{
		int cap = p->cap ? p->cap : 4096;

		while (cap < p->len + extra + 1)
		{
			cap *= 2;
		}

		p->buf = realloc(p->buf, cap);
		p->cap = cap;
	}
}

/* insert text at the read position */
static void
MP_Insert(mparse_t *p, const char *text, int len)
{
	if (len <= 0)
	{
		return;
	}

	MP_Reserve(p, len);
	memmove(p->buf + p->pos + len, p->buf + p->pos, p->len - p->pos + 1);
	memcpy(p->buf + p->pos, text, len);
	p->len += len;
}

static void
MP_Remove(mparse_t *p, int from, int to)
{
	if (to <= from)
	{
		return;
	}

	memmove(p->buf + from, p->buf + to, p->len - to + 1);
	p->len -= to - from;

	if (p->pos > from)
	{
		p->pos = (p->pos >= to) ? p->pos - (to - from) : from;
	}
}

qboolean
MP_ReadFile(const char *path, char **text, int *len)
{
	void *buf;
	int l = FS_LoadFile(path, &buf);

	if (l < 0 || !buf)
	{
		return false;
	}

	*text = malloc(l + 1);
	memcpy(*text, buf, l);
	(*text)[l] = 0;
	*len = l;
	FS_FreeFile(buf);
	return true;
}

/* ------------------------------------------------------------------ */
/* tokeniser (SPEC-800 section 2)                                      */

static qboolean
MP_IsSep(int c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* ^NAME^ at p->pos: replace the reference by its text; inside a quoted
   string (quoted) a double quote of the text would end the string early */
static void
MP_Reference(mparse_t *p, qboolean quoted)
{
	int start = p->pos, i = p->pos + 1, end;
	char name[256];
	int n = 0;
	const char *text;

	while (i < p->len && p->buf[i] != '^')
	{
		int c = (byte)p->buf[i];

		if (MP_IsSep(c) || c == '"')
		{
			MP_Error(p, "Unexpected control char (invalid character in package token)");
			break;
		}

		if (n < (int)sizeof(name) - 1)
		{
			name[n++] = (char)c;
		}

		i++;
	}

	name[n] = 0;
	end = (i < p->len) ? i + 1 : i; /* drops the closing ^ or the invalid character */

	MP_Remove(p, start, end);
	p->pos = start;

	if (++p->expansions > 20000)
	{
		return;
	}

	text = MSP_Find(name);

	if (!text || !text[0])
	{
		MP_Error(p, "Could not find token '%s'", name);
		return;
	}

	/* line breaks in the inserted text become separators; OpenMerc MENU2
	   (docs/DEVIATIONS.md "Quotes in menu texts"): a double quote inserted
	   into a quoted string becomes an apostrophe (localised packages quote
	   names, e.g. the Russian assassin settings title) */
	{
		int l = (int)strlen(text), j;
		char *copy = malloc(l + 1);

		for (j = 0; j < l; j++)
		{
			copy[j] = (text[j] == '\n' || text[j] == '\r') ? ' ' : text[j];

			if (quoted && copy[j] == '"')
			{
				copy[j] = '\'';
			}
		}

		copy[l] = 0;
		MP_Insert(p, copy, l);
		free(copy);
	}
}

/* returns 0 at the end of the text, 1 for a word, 2 for a control char
   (out = "<" or ">") */
int
MP_Token(mparse_t *p, char *out)
{
	int n = 0;
	qboolean quoted = false, any = false;

	for (;;)
	{
		while (p->pos < p->len && MP_IsSep((byte)p->buf[p->pos]))
		{
			if (p->buf[p->pos] == '\n')
			{
				p->line++;
			}

			p->pos++;
		}

		if (p->pos < p->len && p->buf[p->pos] == '^')
		{
			/* a reference at the start of a token: expand, then look again */
			MP_Reference(p, false);
			continue;
		}

		break;
	}

	p->lastpos = p->pos;
	out[0] = 0;

	if (p->pos >= p->len)
	{
		return 0;
	}

	if (p->buf[p->pos] == '<' || p->buf[p->pos] == '>')
	{
		out[0] = p->buf[p->pos++];
		out[1] = 0;
		return 2;
	}

	while (p->pos < p->len)
	{
		int c = (byte)p->buf[p->pos];

		if (c == '"')
		{
			p->pos++;
			any = true;

			if (quoted)
			{
				break;
			}

			quoted = true;
			continue;
		}

		if (c == '<' || c == '>')
		{
			break;
		}

		if (c == '^')
		{
			MP_Reference(p, quoted);
			continue;
		}

		if (MP_IsSep(c))
		{
			if (!quoted)
			{
				break;
			}

			if (c == '\n')
			{
				p->line++;
			}

			c = ' ';
		}

		if (n < MENU_MAX_TOKEN - 1)
		{
			out[n++] = (char)c;
		}

		any = true;
		p->pos++;
	}

	out[n] = 0;

	if (!any && p->pos >= p->len)
	{
		return 0;
	}

	return 1;
}

void
MP_Unread(mparse_t *p)
{
	p->pos = p->lastpos;
}

/* read n arguments; a control char ends the request early */
qboolean
MP_Args(mparse_t *p, char out[][MENU_MAX_TOKEN], int n)
{
	int i, t;

	for (i = 0; i < n; i++)
	{
		out[i][0] = 0;
	}

	for (i = 0; i < n; i++)
	{
		t = MP_Token(p, out[i]);

		if (t == 2)
		{
			out[i][0] = 0;
			MP_Unread(p);

			if (i > 0)
			{
				MP_Error(p, "Unexpected control char (not enough params)");
			}

			return false;
		}

		if (t == 0)
		{
			return false;
		}
	}

	return true;
}

/* the closing '>' of a tag with a fixed number of arguments */
void
MP_EndTag(mparse_t *p)
{
	char tok[MENU_MAX_TOKEN];
	int t = MP_Token(p, tok);

	if (t == 0 || (t == 2 && tok[0] == '>'))
	{
		return;
	}

	MP_Error(p, "Spurious parameters");

	if (t == 2)
	{
		MP_Unread(p);
	}
}

/* skip everything up to and including the next '>' */
void
MP_SkipTag(mparse_t *p)
{
	char tok[MENU_MAX_TOKEN];
	int t;

	while ((t = MP_Token(p, tok)) != 0)
	{
		if (t == 2)
		{
			if (tok[0] == '<')
			{
				MP_Unread(p);
			}

			return;
		}
	}
}

/* ------------------------------------------------------------------ */
/* values                                                              */

mcolor_t
MP_Color(mscreen_t *scr, const char *arg)
{
	mtint_t *t;

	if (scr)
	{
		for (t = scr->tints; t; t = t->next)
		{
			if (!strcmp(t->name, arg))
			{
				return t->value;
			}
		}
	}

	return (mcolor_t)strtoul(arg, NULL, 0);
}

int
MP_ScaleInt(mscreen_t *scr, float v)
{
	return (int)(v * scr->scale);
}

static int
MP_KeyNum(const char *name)
{
	if (!Q_stricmp(name, "any"))
	{
		return -1;
	}

	return Key_StringToKeynum(name);
}

/* ------------------------------------------------------------------ */
/* inclusion (SPEC-800 5.1)                                            */

static void
MP_IncludeFile(mparse_t *p, const char *name)
{
	char path[MAX_QPATH * 2];
	char *text;
	int len;

	Com_sprintf(path, sizeof(path), "menus/%s.rmf", name);

	if (!MP_ReadFile(path, &text, &len))
	{
		MP_Error(p, "Unable to include %s", name);
		return;
	}

	MP_Insert(p, text, len);
	free(text);
}

static void
Tag_Include(mparse_t *p)
{
	char a[1][MENU_MAX_TOKEN];
	MP_Args(p, a, 1);
	MP_EndTag(p);
	MP_IncludeFile(p, a[0]);
}

static void
Tag_CInclude(mparse_t *p)
{
	char a[2][MENU_MAX_TOKEN];
	MP_Args(p, a, 2);
	MP_EndTag(p);

	if (Cvar_VariableValue(a[0]) != 0)
	{
		MP_IncludeFile(p, a[1]);
	}
}

static void
Tag_CNInclude(mparse_t *p)
{
	char a[2][MENU_MAX_TOKEN];
	MP_Args(p, a, 2);
	MP_EndTag(p);

	if (Cvar_VariableValue(a[0]) == 0)
	{
		MP_IncludeFile(p, a[1]);
	}
}

static void
Tag_ExInclude(mparse_t *p)
{
	char a[3][MENU_MAX_TOKEN];
	MP_Args(p, a, 3);
	MP_EndTag(p);
	MP_IncludeFile(p, Cvar_VariableValue(a[0]) != 0 ? a[1] : a[2]);
}

static void
Tag_EInclude(mparse_t *p)
{
	char a[3][MENU_MAX_TOKEN];
	MP_Args(p, a, 3);
	MP_EndTag(p);

	if (!Q_stricmp(Cvar_VariableString(a[0]), a[1]))
	{
		MP_IncludeFile(p, a[2]);
	}
}

static void
Tag_IncludeCvar(mparse_t *p)
{
	char a[1][MENU_MAX_TOKEN];
	const char *s;

	MP_Args(p, a, 1);
	MP_EndTag(p);
	s = Cvar_VariableString(a[0]);
	MP_Insert(p, s, (int)strlen(s));
}

/* ------------------------------------------------------------------ */
/* MENU-SCRATCH: data-driven option tables (docs/impl/MENU2.md "Option
   tables"). <omtable "menus/om_video.tbl"> reads a UTF-8 table and inserts
   the RMF of its rows at the parse position, the labels in the menu language
   (SP_RussianActive) converted to the CP1251 of the fonts. One row per line,
   fields separated by '|', '#' starts a comment line:
     section | EN | RU [| flags]
     list    | cvar | values | labels EN | labels RU | label EN | label RU [| flags]
     slider  | cvar | min | max | step | label EN | label RU [| flags]
     note    | EN | RU [| flags]
     rmf     | raw RMF text
   flags: "stage" (a staged menu_* cvar of the page: marks the page changed),
   "requires=cvar" (row left out while the cvar does not exist, so options of
   an optional renderer show up only where they are registered),
   "tip=EN/RU" (a tip, EN and RU separated by '/'). */

qboolean SP_RussianActive(void);
cvar_t *Cvar_Find(const char *name);

typedef struct
{
	char *buf;
	int len, cap;
} mtbuf_t;

static void
MT_Add(mtbuf_t *b, const char *s)
{
	int l = (int)strlen(s);

	if (b->len + l + 1 > b->cap)
	{
		b->cap = (b->len + l + 1) * 2 + 1024;
		b->buf = realloc(b->buf, b->cap);
	}

	memcpy(b->buf + b->len, s, l + 1);
	b->len += l;
}

/* UTF-8 to the CP1251 of the menu fonts (no glyph for Ё/ё: Е/е);
   a double quote would end the RMF string and becomes an apostrophe */
static void
MT_ToFont(const char *in, char *out, int size)
{
	const unsigned char *s = (const unsigned char *)in;
	int o = 0;

	while (*s && o < size - 1)
	{
		unsigned c = *s++;

		if (c >= 0xC0 && (*s & 0xC0) == 0x80)
		{
			unsigned u = ((c & 0x1F) << 6) | (*s++ & 0x3F);

			if (c >= 0xE0)
			{
				while ((*s & 0xC0) == 0x80)
				{
					s++;
				}

				out[o++] = '?';
				continue;
			}

			if (u >= 0x410 && u <= 0x44F)
			{
				out[o++] = (char)(0xC0 + (u - 0x410));
			}
			else if (u == 0x401)
			{
				out[o++] = (char)0xC5;
			}
			else if (u == 0x451)
			{
				out[o++] = (char)0xE5;
			}
			else
			{
				out[o++] = '?';
			}

			continue;
		}

		out[o++] = (c == '"') ? '\'' : (char)c;
	}

	out[o] = 0;
}

static char *
MT_Trim(char *s)
{
	char *e;

	while (*s == ' ' || *s == '\t')
	{
		s++;
	}

	e = s + strlen(s);

	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
	{
		*--e = 0;
	}

	return s;
}

#define MT_MAXF 12

/* MENU-SCRATCH: half width of the two-column rows (label | value), virtual pixels */
#define MT_SPLIT 230

static void
MT_Row(mtbuf_t *b, char **f, int n, qboolean ru)
{
	char tmp[1024], l1[512], l2[512], tip[512] = "";
	const char *k = f[0];
	qboolean stage = false;
	int fixed, i;

	fixed = !Q_stricmp(k, "list") ? 7 : !Q_stricmp(k, "slider") ? 7 :
		(!Q_stricmp(k, "section") || !Q_stricmp(k, "note")) ? 3 : 0;

	if (!Q_stricmp(k, "rmf"))
	{
		if (n > 1)
		{
			MT_Add(b, f[1]);
			MT_Add(b, "\n");
		}

		return;
	}

	if (!fixed || n < fixed)
	{
		Com_Printf("omtable: bad row \"%s\"\n", k);
		return;
	}

	for (i = fixed; i < n; i++)
	{
		if (!Q_strncasecmp(f[i], "requires=", 9))
		{
			if (!Cvar_Find(f[i] + 9))
			{
				return;
			}
		}
		else if (!Q_stricmp(f[i], "stage"))
		{
			stage = true;
		}
		else if (!Q_strncasecmp(f[i], "tip=", 4))
		{
			char *sl = strchr(f[i] + 4, '/');
			char t[512];

			Q_strlcpy(t, f[i] + 4, sizeof(t));

			if (sl)
			{
				t[sl - (f[i] + 4)] = 0;
			}

			MT_ToFont(ru && sl ? sl + 1 : t, tip, sizeof(tip));
		}
	}

	if (!Q_stricmp(k, "section"))
	{
		MT_ToFont(ru ? f[2] : f[1], l1, sizeof(l1));
		Com_sprintf(tmp, sizeof(tmp), "<hbr>\n<font type title tint orange atint orange>\n"
				"<text \"%s\"%s%s%s>\n<hr width %d tint gray>\n"
				"<font type title tint normaltext atint hilitetext>\n",
				l1, tip[0] ? " tip \"" : "", tip, tip[0] ? "\"" : "", 2 * MT_SPLIT);
	}
	else if (!Q_stricmp(k, "note"))
	{
		MT_ToFont(ru ? f[2] : f[1], l1, sizeof(l1));
		Com_sprintf(tmp, sizeof(tmp), "<font type small tint gray atint gray>\n<text \"%s\"><br>\n"
				"<font type title tint normaltext atint hilitetext>\n", l1);
	}
	else if (!Q_stricmp(k, "list"))
	{
		MT_ToFont(ru ? f[4] : f[3], l1, sizeof(l1));
		MT_ToFont(ru ? f[6] : f[5], l2, sizeof(l2));
		Com_sprintf(tmp, sizeof(tmp), "<list \"%s\" match \"%s\" atext \"%s\" split %d cvar %s%s%s%s%s><hbr>\n",
				l1, f[2], l2, MT_SPLIT, f[1], stage ? " key mouse1 \"set menu_settings_changed 1\"" : "",
				tip[0] ? " tip \"" : "", tip, tip[0] ? "\"" : "");
	}
	else if (MWD_Enabled())
	{
		/* MENU-SCRATCH: wide layout: the label left of the centre, the flat bar right of it */
		MT_ToFont(ru ? f[6] : f[5], l2, sizeof(l2));
		Com_sprintf(tmp, sizeof(tmp), "<center>\n<font type title tint normaltext atint hilitetext>\n"
				"<slider atext \"%s\" split %d cvar %s min %s max %s step %s%s%s%s%s><hbr>\n",
				l2, MT_SPLIT, f[1], f[2], f[3], f[4], stage ? " key mouse1 \"set menu_settings_changed 1\"" : "",
				tip[0] ? " tip \"" : "", tip, tip[0] ? "\"" : "");
	}
	else
	{
		MT_ToFont(ru ? f[6] : f[5], l2, sizeof(l2));
		/* MENU-SCRATCH: centred like the list rows; left-aligned sliders put
		   the colour block at the far left of a wide page, its test picture
		   in the middle and the buttons at the far right */
		Com_sprintf(tmp, sizeof(tmp), "<center>\n<font type title tint normaltext atint normaltext>\n"
				"\" %s\"<br><slider cvar %s min %s max %s step %s%s%s%s%s><br>\n<center>\n"
				"<font type title tint normaltext atint hilitetext>\n",
				l2, f[1], f[2], f[3], f[4], stage ? " key mouse1 \"set menu_settings_changed 1\"" : "",
				tip[0] ? " tip \"" : "", tip, tip[0] ? "\"" : "");
	}

	MT_Add(b, tmp);
}

/* builds the RMF of a table; the result is malloc'ed (NULL: no file) */
char *
MP_TableRMF(const char *path)
{
	char *text, *line, *next;
	int len;
	mtbuf_t b = {NULL, 0, 0};
	qboolean ru = SP_RussianActive();

	if (!MP_ReadFile(path, &text, &len))
	{
		return NULL;
	}

	MT_Add(&b, "");

	for (line = text; line && *line; line = next)
	{
		char *f[MT_MAXF], *c;
		int n = 0;

		next = strchr(line, '\n');

		if (next)
		{
			*next++ = 0;
		}

		line = MT_Trim(line);

		if (!line[0] || line[0] == '#')
		{
			continue;
		}

		if (!Q_strncasecmp(line, "rmf", 3) && (line[3] == ' ' || line[3] == '\t' || line[3] == '|'))
		{
			c = strchr(line, '|');
			f[n++] = "rmf";
			f[n++] = c ? MT_Trim(c + 1) : "";
			MT_Row(&b, f, n, ru);
			continue;
		}

		for (c = line; n < MT_MAXF;)
		{
			char *bar = strchr(c, '|');

			if (bar)
			{
				*bar = 0;
			}

			f[n++] = MT_Trim(c);

			if (!bar)
			{
				break;
			}

			c = bar + 1;
		}

		MT_Row(&b, f, n, ru);
	}

	free(text);
	return b.buf;
}

static void
Tag_OmTable(mparse_t *p)
{
	char a[1][MENU_MAX_TOKEN];
	char *rmf;

	MP_Args(p, a, 1);
	MP_EndTag(p);
	rmf = MP_TableRMF(a[0]);

	if (!rmf)
	{
		MP_Error(p, "Unable to read table %s", a[0]);
		return;
	}

	MP_Insert(p, rmf, (int)strlen(rmf));
	free(rmf);
}

/* ------------------------------------------------------------------ */
/* commands, cvars, configuration (SPEC-800 5.2)                        */

static void
Tag_Set(mparse_t *p)
{
	char a[2][MENU_MAX_TOKEN];

	MP_Args(p, a, 2);
	MP_EndTag(p);

	if (!p->dry)
	{
		M_ExecNow(va("set %s %s", a[0], a[1][0] ? a[1] : "\"\""));
	}
}

static void
Tag_FSet(mparse_t *p)
{
	char a[3][MENU_MAX_TOKEN];

	MP_Args(p, a, 3);
	MP_EndTag(p);

	if (!p->dry)
	{
		M_ExecNow(va("set %s %s %s", a[0], a[1], a[2]));
	}
}

static void
Tag_Alias(mparse_t *p)
{
	char a[2][MENU_MAX_TOKEN];

	MP_Args(p, a, 2);
	MP_EndTag(p);

	if (!p->dry)
	{
		M_ExecNow(va("alias \"%s\" \"%s\"", a[0], a[1]));
	}
}

static void
Tag_CBuf(mparse_t *p)
{
	char a[1][MENU_MAX_TOKEN];

	MP_Args(p, a, 1);
	MP_EndTag(p);

	if (!p->dry)
	{
		M_QueueCommand(a[0], true);
	}
}

/* exec a configuration file synchronously */
void
MP_ExecConfig(const char *name)
{
	char path[MAX_QPATH * 2];
	char *text, *s, line[1024];
	int len;

	Q_strlcpy(path, name, sizeof(path));

	if (strlen(path) < 4 || Q_stricmp(path + strlen(path) - 4, ".cfg"))
	{
		Q_strlcat(path, ".cfg", sizeof(path));
	}

	if (!MP_ReadFile(path, &text, &len))
	{
		Com_Printf("Unable to exec %s\n", path);
		return;
	}

	Com_Printf("execing %s\n", path);

	/* split into commands at ';' and line ends outside quotes */
	s = text;

	while (*s)
	{
		int n = 0;
		qboolean quoted = false;

		while (*s && n < (int)sizeof(line) - 1)
		{
			if (*s == '"')
			{
				quoted = !quoted;
			}

			if ((!quoted && *s == ';') || *s == '\n' || *s == '\r')
			{
				break;
			}

			line[n++] = *s++;
		}

		line[n] = 0;

		if (*s)
		{
			s++;
		}

		if (n > 0)
		{
			M_ExecNow(line);
		}
	}

	free(text);
}

qboolean mp_rebuilding;

static void
Tag_Config(mparse_t *p)
{
	char a[1][MENU_MAX_TOKEN];

	MP_Args(p, a, 1);
	MP_EndTag(p);
	Q_strlcpy(p->screen->config, a[0], sizeof(p->screen->config));

	/* MENU-SCRATCH: a screen created again for a new layout (M_RebuildScreens)
	   keeps the staged values of its open page - running the config again
	   would reload them from the live cvars and drop unapplied changes */
	if (!p->dry && !mp_rebuilding)
	{
		MP_ExecConfig(a[0]);
	}
}

static void
Tag_ExitCfg(mparse_t *p)
{
	char a[1][MENU_MAX_TOKEN];

	MP_Args(p, a, 1);
	MP_EndTag(p);
	Q_strlcpy(p->screen->exitcfg, a[0], sizeof(p->screen->exitcfg));
}

static void
Tag_OnExit(mparse_t *p)
{
	char a[2][MENU_MAX_TOKEN];

	MP_Args(p, a, 2);
	MP_EndTag(p);
	Q_strlcpy(p->screen->onexitcvar, a[0], sizeof(p->screen->onexitcvar));
	Q_strlcpy(p->screen->onexitmenu, a[1], sizeof(p->screen->onexitmenu));

	if (!p->dry && a[0][0])
	{
		Cvar_Get(a[0], "0", 0);
	}
}

static void
Tag_Timeout(mparse_t *p)
{
	char a[2][MENU_MAX_TOKEN];

	MP_Args(p, a, 2);
	MP_EndTag(p);

	if (p->dry)
	{
		return;
	}

	p->screen->hastimeout = true;
	p->screen->timeout = (float)atof(a[0]);

	if (p->screen->timeoutcmd)
	{
		Z_Free(p->screen->timeoutcmd);
	}

	p->screen->timeoutcmd = CopyString(a[1]);
}

static void
Tag_Strings(mparse_t *p)
{
	char a[1][MENU_MAX_TOKEN];

	MP_Args(p, a, 1);
	MP_EndTag(p);
	MSP_Register(a[0]);
}

/* ------------------------------------------------------------------ */
/* text appearance and layout state (SPEC-800 5.3)                      */

static void
Tag_Font(mparse_t *p)
{
	char kw[MENU_MAX_TOKEN], a[1][MENU_MAX_TOKEN];
	mpage_t *pg = p->page;
	int t;

	while ((t = MP_Token(p, kw)) != 0)
	{
		if (t == 2)
		{
			if (kw[0] == '<')
			{
				MP_Unread(p);
			}

			return;
		}

		if (!Q_stricmp(kw, "type"))
		{
			MP_Args(p, a, 1);
			Q_strlcpy(pg->font, a[0], sizeof(pg->font));
		}
		else if (!Q_stricmp(kw, "tint"))
		{
			MP_Args(p, a, 1);
			pg->ftint = MP_Color(p->screen, a[0]);
		}
		else if (!Q_stricmp(kw, "atint"))
		{
			MP_Args(p, a, 1);
			pg->fatint = MP_Color(p->screen, a[0]);
		}
		else
		{
			MP_Error(p, "Unrecognised font keyword");
			return;
		}
	}
}

static void
Tag_Br(mparse_t *p)
{
	MP_EndTag(p);
	ML_BreakLine(p->page, false);
}

static void
Tag_HBr(mparse_t *p)
{
	MP_EndTag(p);
	ML_BreakLine(p->page, true);
}

static void
Tag_Normal(mparse_t *p)
{
	MP_EndTag(p);
	ML_SetAlign(p->page, MA_NORMAL);
}

static void
Tag_Center(mparse_t *p)
{
	MP_EndTag(p);
	ML_SetAlign(p->page, MA_CENTER);
}

static void
Tag_Left(mparse_t *p)
{
	MP_EndTag(p);
	ML_SetAlign(p->page, MA_LEFT);
}

static void
Tag_Right(mparse_t *p)
{
	MP_EndTag(p);
	ML_SetAlign(p->page, MA_RIGHT);
}

static void
Tag_Tab(mparse_t *p)
{
	char tok[MENU_MAX_TOKEN];
	mpage_t *pg = p->page;
	int t;

	while ((t = MP_Token(p, tok)) != 0)
	{
		if (t == 2)
		{
			if (tok[0] == '<')
			{
				MP_Unread(p);
			}

			return;
		}

		if (pg->numtabs < MENU_MAX_TABS)
		{
			float v = (float)atof(tok);

			/* MENU-SCRATCH: columns widen with the wide layout's main area */
			if (p->screen && p->screen->wide && pg->frame && Q_stricmp(pg->frame->name, "topic"))
			{
				v *= p->screen->wg.sx / (pg->zoom > 1.0f ? pg->zoom : 1.0f);
			}

			pg->tabs[pg->numtabs++] = MP_ScaleInt(p->screen, v);
		}
	}
}

static void
Tag_AutoScroll(mparse_t *p)
{
	char a[1][MENU_MAX_TOKEN];

	MP_Args(p, a, 1);
	MP_EndTag(p);
	p->page->autoscroll = true;
	p->page->autorate = (float)atof(a[0]);
}

static void
Tag_Cursor(mparse_t *p)
{
	char a[1][MENU_MAX_TOKEN];
	int n;

	MP_Args(p, a, 1);
	MP_EndTag(p);
	n = atoi(a[0]);
	p->page->cursor = (n <= 0) ? 0 : 1;
	p->screen->cursor = p->page->cursor;
}

static void
Tag_Defaults(mparse_t *p)
{
	char kw[MENU_MAX_TOKEN], a[3][MENU_MAX_TOKEN];
	mpage_t *pg = p->page;
	int t;

	while ((t = MP_Token(p, kw)) != 0)
	{
		if (t == 2)
		{
			if (kw[0] == '<')
			{
				MP_Unread(p);
			}

			return;
		}

		if (!Q_stricmp(kw, "border"))
		{
			MP_Args(p, a, 3);
			pg->dborder = true;
			pg->dbpad = atoi(a[0]);
			pg->dbthick = atoi(a[1]);
			pg->dbcolor = MP_Color(p->screen, a[2]);
		}
		else if (!Q_stricmp(kw, "noborder"))
		{
			pg->dborder = false;
		}
		else if (!Q_stricmp(kw, "tiptint"))
		{
			MP_Args(p, a, 1);
			p->screen->tiptint = MP_Color(p->screen, a[0]);
		}
		else if (!Q_stricmp(kw, "tipbtint"))
		{
			MP_Args(p, a, 1);
			p->screen->tipbtint = MP_Color(p->screen, a[0]);
		}
		else if (!Q_stricmp(kw, "tipfont"))
		{
			MP_Args(p, a, 1);
			Q_strlcpy(p->screen->tipfont, a[0], sizeof(p->screen->tipfont));
		}
		else
		{
			MP_Error(p, "Unrecognised default parameter");
		}
	}
}

static void
Tag_Backdrop(mparse_t *p)
{
	static const char *modes[] = {"tile", "stretch", "center", "left", "right", NULL};
	char kw[MENU_MAX_TOKEN], a[1][MENU_MAX_TOKEN];
	mpage_t *pg = p->page;
	int t, i;

	pg->backdrop = true;

	while ((t = MP_Token(p, kw)) != 0)
	{
		if (t == 2)
		{
			if (kw[0] == '<')
			{
				MP_Unread(p);
			}

			return;
		}

		if (!Q_stricmp(kw, "bgcolor"))
		{
			MP_Args(p, a, 1);
			pg->bgcolor = MP_Color(p->screen, a[0]);
			continue;
		}

		/* MENU-SCRATCH (owner t95u): the own background (m_wide.c
		   MWD_DrawBackground), no picture */
		if (!Q_stricmp(kw, "omflat"))
		{
			pg->bgmode = 1;
			pg->bgpic[0] = 0;
			pg->bgflat = true;
			continue;
		}

		for (i = 0; modes[i]; i++)
		{
			if (!Q_stricmp(kw, modes[i]))
			{
				break;
			}
		}

		if (!modes[i])
		{
			continue; /* other words (the stock "global") are ignored */
		}

		MP_Args(p, a, 1);
		pg->bgmode = i + 1;
		Com_sprintf(pg->bgpic, sizeof(pg->bgpic), "pics/menus/%s", a[0]);

		if (!p->dry)
		{
			int w, h;

			MR_GetPicSize(&w, &h, pg->bgpic);

			if (w <= 0)
			{
				Com_Printf("Warning: Invalid background image\n");
				pg->bgpic[0] = 0;
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* keys (SPEC-800 5.4)                                                 */

static void
Tag_Key(mparse_t *p)
{
	char a[2][MENU_MAX_TOKEN];
	mbind_t *b;

	MP_Args(p, a, 2);
	MP_EndTag(p);
	b = ML_NewBind(MP_KeyNum(a[0]), a[1], NULL, NULL, false);
	b->next = p->page->binds;
	p->page->binds = b;
}

static void
Tag_IKey(mparse_t *p)
{
	char a[2][MENU_MAX_TOKEN];

	MP_Args(p, a, 2);
	MP_EndTag(p);
	ML_AddIKey(&p->page->binds, a[0], a[1]);
}

static void
Tag_CKey(mparse_t *p)
{
	char a[4][MENU_MAX_TOKEN];
	mbind_t *b;

	MP_Args(p, a, 4);
	MP_EndTag(p);

	if (!p->dry)
	{
		Cvar_Get(a[1], "0", 0);
	}

	b = ML_NewBind(MP_KeyNum(a[0]), a[2], a[3], a[1], true);
	b->next = p->page->binds;
	p->page->binds = b;
}

/* ------------------------------------------------------------------ */
/* <stm> (SPEC-800 5.5)                                                */

static void
Tag_Stm(mparse_t *p)
{
	char kw[MENU_MAX_TOKEN], a[2][MENU_MAX_TOKEN];
	mscreen_t *scr = p->screen;
	int t;

	while ((t = MP_Token(p, kw)) != 0)
	{
		if (t == 2)
		{
			if (kw[0] == '<')
			{
				MP_Unread(p);
			}

			return;
		}

		if (!Q_stricmp(kw, "resize"))
		{
			MP_Args(p, a, 2);

			if (p->page->frame != scr->frames[0])
			{
				MP_Error(p, "Can only resize default frame");
				continue;
			}

			{
				mframe_t *f = scr->frames[0];
				int w = atoi(a[0]), h = atoi(a[1]);

				/* MENU-SCRATCH: the dialog keeps its RMF size and is centred over
				   the panel it opens on (it was left-anchored and stretched) */
				f->w = f->fw = w;
				f->h = f->fh = h;
				f->x = f->fx = M_DialogCentreX(scr) - w / 2;
				f->y = f->fy = (MR_VirtualH() - h) >> 1;
				scr->scale = 1.0f;
				scr->resize = true;
				p->page->contenth = h;
			}
		}
		else if (!Q_stricmp(kw, "nopush"))
		{
			scr->nopush = true;
		}
		else if (!Q_stricmp(kw, "waitanim"))
		{
			if (!p->dry)
			{
				scr->ready = false;
			}
		}
		else if (!Q_stricmp(kw, "fragile"))
		{
			scr->fragile = true;
		}
		else if (!Q_stricmp(kw, "global"))
		{
			p->page->global = true;
		}
		else if (!Q_stricmp(kw, "command"))
		{
			MP_Args(p, a, 1);

			if (!p->dry)
			{
				M_ExecNow(a[0]);
			}
		}
		/* unknown <stm> options are ignored silently */
	}
}

static void
Tag_Tint(mparse_t *p)
{
	char a[2][MENU_MAX_TOKEN];
	mtint_t *t;

	MP_Args(p, a, 2);
	MP_EndTag(p);

	for (t = p->screen->tints; t; t = t->next)
	{
		if (!strcmp(t->name, a[0]))
		{
			break;
		}
	}

	if (!t)
	{
		t = Z_Malloc(sizeof(*t));
		Q_strlcpy(t->name, a[0], sizeof(t->name));
		t->next = p->screen->tints;
		p->screen->tints = t;
	}

	t->value = (mcolor_t)strtoul(a[1], NULL, 0);
}

static void
Tag_Comment(mparse_t *p)
{
	MP_SkipTag(p);
}

/* ------------------------------------------------------------------ */
/* frames (SPEC-800 section 8)                                         */

mframe_t *
MP_FindFrame(mscreen_t *scr, const char *name)
{
	int i;

	for (i = 0; i < scr->numframes; i++)
	{
		if (!Q_stricmp(scr->frames[i]->name, name))
		{
			return scr->frames[i];
		}
	}

	return NULL;
}

/* cut the child out of the parent's free rectangle */
static void
MP_CutFrame(mframe_t *parent, mframe_t *f, int W, int H, int w, int h)
{
	int pp = parent->pad;

	f->x = parent->fx + pp;
	f->y = parent->fy + pp;

	if (parent->fw == 0 || parent->fh == 0)
	{
		f->w = f->h = 0;
	}
	else
	{
		f->w = (w >= 1) ? w : parent->fw - 2 * pp + w;
		f->h = (h >= 1) ? h : parent->fh - 2 * pp + h;
	}

	if (W != 0)
	{
		parent->fx += f->w;
		parent->fw -= f->w;
	}

	if (W == 0)
	{
		parent->fy += f->h;
		parent->fh -= f->h;
	}

	f->fx = f->x;
	f->fy = f->y;
	f->fw = f->w;
	f->fh = f->h;
}

/* page history of a frame (SPEC-801 section 2) */
qboolean
MP_ShowPage(mscreen_t *scr, mframe_t *f, const char *name, qboolean dry)
{
	int i;
	mpage_t *pg;

	for (i = 0; i < f->numpages; i++)
	{
		if (!Q_stricmp(f->pages[i]->name, name))
		{
			while (f->numpages > i + 1)
			{
				MP_FreePage(f->pages[--f->numpages]);
			}

			pg = f->pages[i];

			for (int j = 0; j < pg->numwidgets; j++)
			{
				if (pg->widgets[j]->cls->refresh)
				{
					pg->widgets[j]->cls->refresh(pg, pg->widgets[j]);
				}
			}

			M_MarkTabDirty(scr);
			return true;
		}
	}

	pg = MP_LoadPage(scr, f, name, dry);

	if (!pg)
	{
		return false;
	}

	if (f->numpages == MENU_MAX_HISTORY)
	{
		MP_FreePage(f->pages[0]);
		memmove(f->pages, f->pages + 1, sizeof(f->pages[0]) * (MENU_MAX_HISTORY - 1));
		f->numpages--;
	}

	f->pages[f->numpages++] = pg;
	M_MarkTabDirty(scr);
	return true;
}

static void
Tag_Frame(mparse_t *p)
{
	char a[3][MENU_MAX_TOKEN], kw[MENU_MAX_TOKEN], o[3][MENU_MAX_TOKEN];
	mscreen_t *scr = p->screen;
	mframe_t *f, *parent;
	qboolean exists, hasborder = false, nopage = false;
	char page[MAX_QPATH] = "", cut[64] = "";
	int W, H, t;

	MP_Args(p, a, 3);

	if (!Q_stricmp(a[0], "default"))
	{
		MP_Error(p, "Not allowed to alter default frame");
		MP_SkipTag(p);
		return;
	}

	f = MP_FindFrame(scr, a[0]);
	exists = (f != NULL);
	W = atoi(a[1]);
	H = atoi(a[2]);

	if (!exists)
	{
		if (scr->numframes >= MENU_MAX_FRAMES)
		{
			MP_SkipTag(p);
			return;
		}

		f = Z_Malloc(sizeof(*f));
		Q_strlcpy(f->name, a[0], sizeof(f->name));
		f->screen = scr;
		f->backfill = 0xFF000000;
		f->w = MP_ScaleInt(scr, (float)W);
		f->h = MP_ScaleInt(scr, (float)H);
	}

	while ((t = MP_Token(p, kw)) != 0)
	{
		if (t == 2)
		{
			if (kw[0] == '<')
			{
				MP_Unread(p);
			}

			break;
		}

		if (!Q_stricmp(kw, "border"))
		{
			MP_Args(p, o, 3);
			f->pad = MP_ScaleInt(scr, (float)atof(o[0]));
			f->thick = atoi(o[1]);
			f->bcolor = MP_Color(scr, o[2]);
			f->border = true;
			hasborder = true;
		}
		else if (!Q_stricmp(kw, "backfill"))
		{
			MP_Args(p, o, 1);

			if (hasborder)
			{
				f->backfill = MP_Color(scr, o[0]);
			}
		}
		else if (!Q_stricmp(kw, "page"))
		{
			MP_Args(p, o, 1);
			Q_strlcpy(page, o[0], sizeof(page));
		}
		else if (!Q_stricmp(kw, "cpage"))
		{
			MP_Args(p, o, 1);
			Q_strlcpy(f->cpage, o[0], sizeof(f->cpage));

			if (!p->dry)
			{
				Q_strlcpy(page, Cvar_Get(o[0], "", 0)->string, sizeof(page));
			}
		}
		else if (!Q_stricmp(kw, "cut"))
		{
			MP_Args(p, o, 1);
			Q_strlcpy(cut, o[0], sizeof(cut));
		}
		/* other words are ignored without a message (frame "center") */
	}

	/* MENU-SCRATCH: the wide layout places topic and main itself (m_wide.c) */
	if (!exists && MWD_Frame(scr, f, cut, W, H, &nopage))
	{
		scr->frames[scr->numframes++] = f;
		exists = true;
	}

	if (!exists)
	{
		parent = cut[0] ? MP_FindFrame(scr, cut) : scr->frames[0];

		if (!parent)
		{
			MP_Error(p, "Could not find parent for \"%s\"", f->name);
			Z_Free(f);
			return;
		}

		MP_CutFrame(parent, f, W, H, f->w, f->h);
		scr->frames[scr->numframes++] = f;
	}

	if (page[0] && !p->dry && !nopage)
	{
		if (!MP_ShowPage(scr, f, MP_ExpandMenuName(page), false))
		{
			MP_Error(p, "Could not create \"%s\"", f->name);
			scr->invalid = true;
		}
	}
}

/* ------------------------------------------------------------------ */
/* page parsing (SPEC-800 sections 1 and 3)                            */

typedef struct
{
	const char *name;
	void (*fn)(mparse_t *p);
} mtag_t;

static const mtag_t mp_tags[] = {
	{"include", Tag_Include},
	{"cinclude", Tag_CInclude},
	{"cninclude", Tag_CNInclude},
	{"exinclude", Tag_ExInclude},
	{"einclude", Tag_EInclude},
	{"includecvar", Tag_IncludeCvar},
	{"omtable", Tag_OmTable},
	{"set", Tag_Set},
	{"fset", Tag_FSet},
	{"alias", Tag_Alias},
	{"cbuf", Tag_CBuf},
	{"config", Tag_Config},
	{"exitcfg", Tag_ExitCfg},
	{"onexit", Tag_OnExit},
	{"timeout", Tag_Timeout},
	{"strings", Tag_Strings},
	{"comment", Tag_Comment},
	{"font", Tag_Font},
	{"br", Tag_Br},
	{"hbr", Tag_HBr},
	{"normal", Tag_Normal},
	{"center", Tag_Center},
	{"left", Tag_Left},
	{"right", Tag_Right},
	{"tab", Tag_Tab},
	{"autoscroll", Tag_AutoScroll},
	{"cursor", Tag_Cursor},
	{"defaults", Tag_Defaults},
	{"backdrop", Tag_Backdrop},
	{"key", Tag_Key},
	{"ikey", Tag_IKey},
	{"ckey", Tag_CKey},
	{"stm", Tag_Stm},
	{"frame", Tag_Frame},
	{"tint", Tag_Tint},
	{NULL, NULL}
};

/* menu names given to "menu" and "page" may contain $cvar (already
   expanded by the console) - nothing to do here, kept as a hook */
const char *
MP_ExpandMenuName(const char *name)
{
	return name;
}

void
MP_ParseText(mparse_t *p)
{
	char tok[MENU_MAX_TOKEN], name[MENU_MAX_TOKEN];
	int t, i;

	while ((t = MP_Token(p, tok)) != 0)
	{
		if (t == 1)
		{
			MW_BareText(p, tok);
			continue;
		}

		t = MP_Token(p, name);

		if (t == 0)
		{
			break;
		}

		if (t == 2)
		{
			MP_Unread(p);
			continue;
		}

		Q_strlcpy(p->tag, name, sizeof(p->tag));

		if (!Q_stricmp(name, "/stm"))
		{
			continue;
		}

		for (i = 0; mp_tags[i].name; i++)
		{
			if (!Q_stricmp(name, mp_tags[i].name))
			{
				break;
			}
		}

		if (mp_tags[i].name)
		{
			mp_tags[i].fn(p);
			continue;
		}

		if (MW_Tag(p, name) || MD_DataTag(p, name) || MM_Tag(p, name))
		{
			continue;
		}

		/* SPEC-803: emote, semote, demote and dmnames of the stock chat/server
		   pages have no handler in the original; accepted and ignored (MENU2) */
		if (!Q_stricmp(name, "emote") || !Q_stricmp(name, "semote") ||
			!Q_stricmp(name, "demote") || !Q_stricmp(name, "dmnames"))
		{
			MP_SkipTag(p);
			continue;
		}

		MP_Error(p, "Unknown keyword");
		MP_SkipTag(p);
	}
}

static const char *
MP_FindCI(const char *hay, int len, const char *needle)
{
	int n = (int)strlen(needle), i;

	for (i = 0; i + n <= len; i++)
	{
		if (!Q_strncasecmp(hay + i, needle, n))
		{
			return hay + i;
		}
	}

	return NULL;
}

void
MP_InitPage(mpage_t *pg, mscreen_t *scr, mframe_t *frame, const char *name)
{
	Q_strlcpy(pg->name, name, sizeof(pg->name));
	pg->screen = scr;
	pg->frame = frame;
	Q_strlcpy(pg->font, "small", sizeof(pg->font));
	pg->ftint = pg->fatint = 0xFFFFFFFF;
	pg->tabs[0] = 0;
	pg->numtabs = 1;
	pg->cursor = -1;
	pg->align = MA_NORMAL;
	pg->contenth = frame ? frame->fh - 2 * frame->pad : 0;
}

static mpage_t *MP_LoadPageZ(mscreen_t *scr, mframe_t *frame, const char *name, qboolean dry, float zoom);

/* bounding size of the placed widgets of a page, pads not included */
static int
MP_PageExtent(const mpage_t *pg, int *W, int *H)
{
	int i, n = 0;

	*W = *H = 0;

	for (i = 0; i < pg->numwidgets; i++)
	{
		const mwidget_t *w = pg->widgets[i];

		if (w->flags & (WF_BOLTED | WF_NOTPLACED))
		{
			continue;
		}

		*W = w->x + ML_TotalW(w) > *W ? w->x + ML_TotalW(w) : *W;
		*H = w->y + ML_TotalH(w) > *H ? w->y + ML_TotalH(w) : *H;
		n++;
	}

	return n;
}

/*
 * MENU-SCRATCH page zoom: a page of a wide screen whose content would sit
 * small in the big main panel (new game, stats, credits) is
 * laid out in its frame divided by z and drawn magnified by z (ML_DrawPage).
 * z is the largest step at which the page, laid out at that size, still
 * fits the frame's width and 85% of its height (an autoscroll page: keeps
 * at least 480 units of width). Pages with a scroll bar, models or the
 * global flag, the tab strip, the rail, the default frame and frames smaller
 * than 3/4 of the main area (the join screen's control column) keep 1.
 * Off by default since the modern scaling (owner t91u: the menus were too
 * large; text is TrueType at a share of the window height): menu_zoompages 1
 * turns it on (a new name, so an archived "menu_pagezoom 1" does not).
 */
static float
MP_PageZoom(mscreen_t *scr, mframe_t *frame, const char *name)
{
	static const float steps[] = {2.0f, 1.75f, 1.5f, 1.25f};
	cvar_t *cv = Cvar_Get("menu_zoompages", "0", CVAR_ARCHIVE);
	int s;

	if (!cv->value || !scr || !frame || !scr->wide || !MR_Virtual() || frame->rail ||
		frame == scr->frames[0] || !Q_stricmp(frame->name, "topic") || frame->fw <= 0 || frame->fh <= 0 ||
		frame->fw * 4 < scr->wg.main.w * 3 || frame->fh * 4 < scr->wg.main.h * 3)
	{
		return 1.0f;
	}

	for (s = 0; s < (int)(sizeof(steps) / sizeof(steps[0])); s++)
	{
		int errs = mp_errorcount;
		mpage_t *t = MP_LoadPageZ(scr, frame, name, true, steps[s]);
		const mframe_t *zf;
		int W, H, n;
		qboolean ok;

		/* a widget that no longer fits the shrunk frame ("Object too wide
		   to place") is dropped by the layout: that step does not fit */
		qboolean dropped = mp_errorcount != errs;

		if (!t)
		{
			return 1.0f;
		}

		if (t->vbar || t->global || t->nummodels)
		{
			MP_FreePage(t);
			return 1.0f;
		}

		zf = t->frame;
		n = MP_PageExtent(t, &W, &H);
		ok = !dropped && n > 0 && W <= zf->fw - 2 * zf->pad &&
			(t->autoscroll ? zf->fw >= 480 : H * 100 <= (zf->fh - 2 * zf->pad) * 85);
		MP_FreePage(t);

		mp_errorcount = errs;	/* the dry probes are not the page's errors */

		if (ok)
		{
			return steps[s];
		}
	}

	return 1.0f;
}

mpage_t *
MP_LoadPage(mscreen_t *scr, mframe_t *frame, const char *name, qboolean dry)
{
	return MP_LoadPageZ(scr, frame, name, dry, dry ? 1.0f : MP_PageZoom(scr, frame, name));
}

static mpage_t *
MP_LoadPageZ(mscreen_t *scr, mframe_t *frame, const char *name, qboolean dry, float zoom)
{
	char path[MAX_QPATH * 2];
	char *text;
	int len;
	const char *start, *end, *b;
	mparse_t p;
	mpage_t *pg;
	int i, j;

	b = strstr(name, "base\\menus\\");

	if (strlen(name) > 4 && !Q_stricmp(name + strlen(name) - 4, ".rmf") && b)
	{
		Q_strlcpy(path, b + 5, sizeof(path));
	}
	else
	{
		Com_sprintf(path, sizeof(path), "menus/%s.rmf", name);
	}

	if (!MP_ReadFile(path, &text, &len))
	{
		return NULL;
	}

	memset(&p, 0, sizeof(p));
	p.line = 1;
	p.screen = scr;
	p.dry = dry;
	Q_strlcpy(p.tag, "", sizeof(p.tag));

	/* only the text from the first "<stm" to the first "</stm" is used */
	start = MP_FindCI(text, len, "<stm");
	end = start ? MP_FindCI(start, len - (int)(start - text), "</stm") : NULL;

	if (!start || !end)
	{
		MP_Error(&p, "File is not in STM format");
		free(text);
		return NULL;
	}

	/* count the lines before the window */
	for (b = text; b < start; b++)
	{
		if (*b == '\n')
		{
			p.line++;
		}
	}

	MP_Reserve(&p, (int)(end - start) + 5);
	memcpy(p.buf, start, end - start + 5);
	p.len = (int)(end - start) + 5;
	p.buf[p.len] = 0;
	free(text);

	pg = Z_Malloc(sizeof(*pg));
	MP_InitPage(pg, scr, frame, name);
	pg->dry = dry;

	if (zoom > 1.0f && frame)
	{
		pg->zframe = Z_Malloc(sizeof(*pg->zframe));
		*pg->zframe = *frame;
		pg->zframe->numpages = 0;
		pg->zframe->w = pg->zframe->fw = (int)((float)frame->fw / zoom);
		pg->zframe->h = pg->zframe->fh = (int)((float)frame->fh / zoom);
		pg->zoom = zoom;
		pg->frame = pg->zframe;
		pg->contenth = pg->frame->fh - 2 * pg->frame->pad;
	}
	p.page = pg;

	MP_ParseText(&p);
	free(p.buf);

	/* end-of-page step of every widget */
	for (i = 0, j = 0; i < pg->numwidgets; i++)
	{
		mwidget_t *w = pg->widgets[i];

		if (w->cls->endpage && !w->cls->endpage(pg, w))
		{
			if (pg->vbar == w)
			{
				pg->vbar = NULL;
			}

			if (pg->focus == w)
			{
				pg->focus = NULL;
			}

			ML_FreeWidget(w);
			continue;
		}

		pg->widgets[j++] = w;
	}

	pg->numwidgets = j;
	scr->opentime = M_Time();

	if (pg->zframe)
	{
		int W, H;

		MP_PageExtent(pg, &W, &H);
		pg->zcontent = H + 2 * pg->frame->pad;

		if (!dry)
		{
			Com_DPrintf("menu: page %s zoom %.2f (%dx%d in %dx%d)\n", name, pg->zoom, W, H,
				pg->frame->fw, pg->frame->fh);
		}
	}

	if (pg->failed && !dry)
	{
		MP_FreePage(pg);
		return NULL;
	}

	return pg;
}

void
MP_FreePage(mpage_t *pg)
{
	int i;

	if (!pg)
	{
		return;
	}

	for (i = 0; i < pg->numwidgets; i++)
	{
		if (ms.capture == pg->widgets[i])
		{
			ms.capture = NULL;
		}

		if (ms.tipwidget == pg->widgets[i])
		{
			ms.tipwidget = NULL;
		}

		if (ms.lasthover == pg->widgets[i])
		{
			ms.lasthover = NULL;
		}

		if (pg->screen)
		{
			for (int k = 0; k < 4; k++)
			{
				if (pg->screen->selections[k] == pg->widgets[i])
				{
					pg->screen->selections[k] = NULL;
				}
			}

			if (pg->screen->tablist)
			{
				pg->screen->tabdirty = true;
			}
		}

		ML_FreeWidget(pg->widgets[i]);
	}

	/* bolted models before the models they hang on */
	for (i = pg->nummodels - 1; i >= 0; i--)
	{
		MM_FreeModel(pg->models[i]);
	}

	ML_FreeBinds(pg->binds);

	if (pg->widgets)
	{
		Z_Free(pg->widgets);
	}

	if (pg->zframe)
	{
		Z_Free(pg->zframe);
	}

	Z_Free(pg);
}
