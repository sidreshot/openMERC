/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc: the plain menu widgets - text, ctext, list, slider, input,
 * setkey, image, hbar, vbar, ticker, hr, blank, selection, popup.
 *
 * Implements: SPEC-802, SPEC-800 (section 7 per-widget parts); SPEC-AMEND-005 I6m.1 (input colours),
 *             I6m.3 (case of listed names)
 */

#include <math.h>

#include "header/menu.h"
#include "header/m_draw.h"
#include "../input/header/input.h"
#include "../vid/header/vid.h"

#define PIC(n) va("pics/menus/%s", n)

static int
MW_FH(const mwidget_t *w)
{
	return MR_FontHeight(w->font);
}

static int
MW_SW(const char *s, const char *font)
{
	return MR_StringWidth(s, font);
}

static qboolean
MW_Hovered(const mwidget_t *w)
{
	return (w->flags & WF_HOVER) != 0;
}

/* latched cvar while connected (SPEC-802 conventions) */
qboolean
MW_Disabled(const mwidget_t *w)
{
	return w->cvar && (w->cvar->flags & CVAR_LATCH) && cls.state >= ca_connected;
}

static int
MW_TabStop(const mpage_t *pg)
{
	return pg->numtabs > 1 ? pg->tabs[1] : 0;
}

/* right edge of the frame's content area on screen */
static int
MW_ContentRight(const mpage_t *pg)
{
	const mframe_t *f = pg->frame;
	return f->fx + f->pad + ML_ContentW(pg) + pg->screen->originx;
}

static mcolor_t
MW_Shade(mcolor_t c)
{
	/* MENU-SCRATCH: labels at 82 % of the value colour (was 70 %: too dark on the flat panels) */
	int r = (int)((c & 0xff) * 0.82f), g = (int)(((c >> 8) & 0xff) * 0.82f), b = (int)(((c >> 16) & 0xff) * 0.82f);
	return (c & 0xff000000) | (b << 16) | (g << 8) | r;
}

/* ------------------------------------------------------------------ */
/* label/value renderer                                                 */

/* MENU-SCRATCH: half the space between the columns of a 'split' row */
static int
MW_SplitGap(const mwidget_t *w)
{
	return MW_FH(w) / 2 + 2;
}

/* width of a 'split' row: twice the half width, widened when the label or
   the widest value does not fit its column */
static int
MW_SplitWidth(mwidget_t *w, int labelw, int valuew, int maxw)
{
	int need = (labelw > valuew ? labelw : valuew) + MW_SplitGap(w);

	if (w->split < need)
	{
		w->split = need;
	}

	/* never wider than the page: a row too wide to place would be dropped */
	if (maxw > 0 && 2 * w->split > maxw)
	{
		w->split = maxw / 2;
	}

	return 2 * w->split;
}

void
MW_LabelValue(mpage_t *pg, mwidget_t *w, int x, int y, const char *label, const char *value,
		qboolean blink, qboolean disabled)
{
	qboolean hover = MW_Hovered(w);
	mcolor_t c, lc, vc;

	if (disabled)
	{
		hover = false;
	}

	c = hover ? w->atint : w->tint;
	lc = (w->flags & WF_NOSHADE) ? c : MW_Shade(c);
	vc = c;

	if (blink)
	{
		vc = (vc & 0x00ffffff) | ((mcolor_t)((M_Time() + 128) & 255) << 24);
	}

	if (disabled)
	{
		lc = vc = (w->tint & 0x00ffffff) | 0x60000000;
	}

	if (w->split > 0 && !(w->flags & WF_TAB))
	{
		int gap = MW_SplitGap(w);

		MR_ClipRight(MW_ContentRight(pg));

		if (hover)
		{
			MR_Fill(x, y, w->w, MW_FH(w), w->btint);
		}

		MR_DrawString(x + w->split - gap - MW_SW(label, w->font), y, label, lc, w->font, 0);
		MR_DrawString(x + w->split + gap, y, value, vc, w->font, 0);
		MR_Reclip();
	}
	else if (!(w->flags & WF_TAB))
	{
		MR_ClipRight(MW_ContentRight(pg));

		if (hover)
		{
			MR_Fill(x, y, w->w, MW_FH(w), w->btint);
		}

		MR_DrawString(x, y, label, lc, w->font, 0);
		MR_DrawString(x + MW_SW(label, w->font), y, value, vc, w->font, 0);
		MR_Reclip();
	}
	else
	{
		int T = MW_TabStop(pg);

		MR_ClipRight(x + T);
		MR_DrawString(x, y, label, lc, w->font, 0);
		MR_ClipRight(MW_ContentRight(pg));
		MR_DrawString(x + T, y, value, vc, w->font, 0);
		MR_Reclip();
	}
}

/* comma lists (SPEC-802): a comma is appended, every comma ends an item */
void
MW_SplitComma(const char *text, char ***items, int *count)
{
	char buf[260];
	char *s, *start;
	int n = 0, cap = 8;
	char **list = Z_Malloc(cap * sizeof(char *));

	Q_strlcpy(buf, text, 256);
	Q_strlcat(buf, ",", sizeof(buf));

	for (s = start = buf; *s; s++)
	{
		if (*s == ',')
		{
			*s = 0;

			if (n == cap)
			{
				char **nl = Z_Malloc(cap * 2 * sizeof(char *));
				memcpy(nl, list, cap * sizeof(char *));
				Z_Free(list);
				list = nl;
				cap *= 2;
			}

			list[n++] = CopyString(start);
			start = s + 1;
		}
	}

	*items = list;
	*count = n;
}

void
MW_FreeList(char **items, int count)
{
	int i;

	if (!items)
	{
		return;
	}

	for (i = 0; i < count; i++)
	{
		Z_Free(items[i]);
	}

	Z_Free(items);
}

static void
MW_SetCvar(cvar_t *v, const char *value)
{
	if (v)
	{
		Cvar_Set(v->name, value);
	}
}

static void
MW_SetCvarValue(cvar_t *v, float value)
{
	if (v)
	{
		Cvar_SetValue(v->name, value);
	}
}

/* ------------------------------------------------------------------ */
/* text                                                                 */

/* MENU-SCRATCH: <text ... icon "pic" size> - a tab icon of the stock menus drawn
   before the label (the navigation rail); white, cyan when hovered or when
   "currentif cvar value" matches at the parse; icon "-" keeps the space
   without a picture (a row with no fitting stock icon) */
typedef struct
{
	char pic[MAX_QPATH];
	int size;
	qboolean current;
	qboolean button;	/* MENU-SCRATCH "ombutton": a landing-page button */
} texticon_t;

static texticon_t *
Text_Icon(mwidget_t *w)
{
	if (!w->data)
	{
		w->data = Z_Malloc(sizeof(texticon_t));
	}

	return w->data;
}

static qboolean
Text_Option(mparse_t *p, mwidget_t *w, const char *kw)
{
	char a[2][MENU_MAX_TOKEN];

	if (!Q_stricmp(kw, "regular"))
	{
		w->flags |= WF_REGULAR;
		w->flags &= ~WF_TEXTLIKE;
		return true;
	}

	if (!Q_stricmp(kw, "icon"))
	{
		texticon_t *d = Text_Icon(w);

		MP_Args(p, a, 2);
		Q_strlcpy(d->pic, strcmp(a[0], "-") ? PIC(a[0]) : "-", sizeof(d->pic));
		d->size = atoi(a[1]);

		if (d->size < 8)
		{
			d->size = 8;
		}

		w->w += d->size + 6;

		if (d->size > w->h)
		{
			w->h = w->minh = d->size;
		}

		return true;
	}

	/* MENU-SCRATCH: "ombutton" draws the text centred on a full-width bar
	   (dark at rest, btint with an orange edge when hovered); the main
	   menu's landing page (menus/m_omhome.rmf) */
	if (!Q_stricmp(kw, "ombutton"))
	{
		Text_Icon(w)->button = true;
		return true;
	}

	if (!Q_stricmp(kw, "currentif"))
	{
		MP_Args(p, a, 2);
		Text_Icon(w)->current = Cvar_VariableValue(a[0]) == (float)atof(a[1]);
		return true;
	}

	return false;
}

static void
Text_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	/* OpenMerc MENU2: the game fonts keep the top of their cell free, so capitals drawn in the middle
	   of a button border sit low; lift them by 2/9 of the cell (4 of 18 for the title font, whose
	   capitals fill the lower 10 rows) so they are centred between the lines */
	if ((w->flags & WF_HASBORDER) && !(w->flags & WF_HIDEBORDER))
	{
		y -= MW_FH(w) * 2 / 9;
	}

	if (w->data && ((texticon_t *)w->data)->button)
	{
		qboolean lit = MW_Hovered(w);
		int fh = MW_FH(w);
		int tw = MR_StringWidth(w->text, w->font);

		MR_Fill(x, y, w->w, w->h, lit ? w->btint : 0x60000000);

		if (lit)
		{
			MR_Fill(x, y, 3, w->h, 0xFF2080FF);
		}

		MR_DrawString(x + (w->w - tw) / 2, y + (w->h - fh) / 2, w->text,
				lit ? w->atint : w->tint, w->font, 0);
		return;
	}

	if (w->data && ((texticon_t *)w->data)->pic[0])
	{
		texticon_t *d = w->data;
		qboolean lit = d->current || MW_Hovered(w);
		int fh = MW_FH(w);

		if (lit)
		{
			/* the whole row (MW_LabelValue would fill only the label) */
			MR_Fill(x - 2, y, w->w + 4, w->h, w->btint);
		}

		if (strcmp(d->pic, "-"))
		{
			MR_StretchPic(x, y, d->size, d->size, lit ? 0xFFFFFF00 : 0xFFFFFFFF, d->pic, 4);
		}

		(void)pg;
		MR_DrawString(x + d->size + 6, y + (d->size - fh) / 2, w->text,
				lit ? w->atint : w->tint, w->font, 0);
		return;
	}

	MW_LabelValue(pg, w, x, y, "", w->text, false, false);
}

static void
Text_Free(mwidget_t *w)
{
	if (w->data)
	{
		Z_Free(w->data);
	}
}

static qboolean
Text_Key(mpage_t *pg, mwidget_t *w, int key)
{
	(void)pg;
	(void)w;
	(void)key;
	return true;
}

static const mwclass_t mw_text = {
	"text", Text_Option, NULL, NULL, NULL, NULL, Text_Draw, Text_Key, NULL, Text_Free
};

static mwidget_t *
MW_NewText(mparse_t *p, const char *text)
{
	mwidget_t *w = ML_NewWidget(p, &mw_text, true);
	int fh = MW_FH(w);

	w->text = CopyString(text);
	w->w = MW_SW(text, w->font) + fh / 2;
	w->h = w->minh = fh;
	w->flags |= WF_TEXTLIKE;
	return w;
}

void
MW_BareText(mparse_t *p, const char *text)
{
	ML_AddWidget(p, MW_NewText(p, text));
}

static void
Tag_Text(mparse_t *p)
{
	char a[1][MENU_MAX_TOKEN];
	mwidget_t *w;

	MP_Args(p, a, 1);
	w = MW_NewText(p, a[0]);
	ML_ReadOptions(p, w);
	ML_AddWidget(p, w);
}

/* ------------------------------------------------------------------ */
/* ctext                                                                */

typedef struct
{
	char *atext;
	qboolean invisible;
	int fixedw;	/* MENU-SCRATCH: an explicit width: the value centred in it */
	char *prev, *next;	/* MENU-SCRATCH: a chooser: "< value >", each half runs its command */
} ctext_t;

/* MENU-SCRATCH: the row width: fixed, two columns ('split') or label + value */
static int
CText_Width(mpage_t *pg, mwidget_t *w)
{
	ctext_t *d = w->data;
	int lw = MW_SW(d->atext ? d->atext : "", w->font), vw = MW_SW(w->cvar ? w->cvar->string : "", w->font);

	if (d->fixedw > 0)
	{
		return d->fixedw;
	}

	if (w->split > 0)
	{
		return MW_SplitWidth(w, lw, vw, ML_ContentW(pg) - 2 * w->bpad);
	}

	return lw + vw;
}

static qboolean
CText_Option(mparse_t *p, mwidget_t *w, const char *kw)
{
	ctext_t *d = w->data;
	char a[1][MENU_MAX_TOKEN];

	if (!Q_stricmp(kw, "atext"))
	{
		MP_Args(p, a, 1);

		if (d->atext)
		{
			Z_Free(d->atext);
		}

		d->atext = CopyString(a[0]);
		return true;
	}

	if (!Q_stricmp(kw, "invisible"))
	{
		d->invisible = true;
		return true;
	}

	if (!Q_stricmp(kw, "prev") || !Q_stricmp(kw, "next"))
	{
		char **dst = !Q_stricmp(kw, "prev") ? &d->prev : &d->next;

		MP_Args(p, a, 1);

		if (*dst)
		{
			Z_Free(*dst);
		}

		*dst = CopyString(a[0]);
		return true;
	}

	return false;
}

static const char *
CText_Value(mwidget_t *w)
{
	return w->cvar ? w->cvar->string : "";
}

static qboolean
CText_Finish(mparse_t *p, mwidget_t *w)
{
	ctext_t *d = w->data;

	if (w->w > 0 && !d->atext)
	{
		d->fixedw = w->w;
	}

	w->w = CText_Width(p->page, w);
	return true;
}

static void
CText_Update(mpage_t *pg, mwidget_t *w)
{
	if (w->cvar && w->cvar->modified)
	{
		w->cvar->modified = false;
		w->w = CText_Width(pg, w);

		if (w->align == MA_CENTER || w->align == MA_RIGHT)
		{
			ML_Reposition(pg, w);
		}
	}
}

static void
CText_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	ctext_t *d = w->data;

	if (d->invisible && !CText_Value(w)[0])
	{
		w->flags |= WF_HIDEBORDER;
		return;
	}

	if (d->fixedw > 0)
	{
		if (d->prev || d->next)
		{
			/* the chooser: a flat bar, the hovered half lit, arrows at the ends */
			float mx, my;
			int sx, sy, aw = MW_SW("<<", w->font), side = 0;

			if (MW_Hovered(w))
			{
				ML_WidgetScreenPos(pg, w, &sx, &sy);
				ML_PageMouse(pg, w, &mx, &my);
				side = ((int)mx - sx < w->w / 2) ? -1 : 1;
			}

			MR_Fill(x, y, w->w, w->h, 0x60000000);

			if (side)
			{
				MR_Fill(side < 0 ? x : x + w->w / 2, y, w->w / 2, w->h, w->btint);
				MR_Fill(side < 0 ? x : x + w->w - 3, y, 3, w->h, 0xFF2080FF);
			}

			MR_DrawString(x + aw / 2, y + (w->h - MW_FH(w)) / 2, "\xab", side < 0 ? w->atint : w->tint, w->font, 0);
			MR_DrawString(x + w->w - aw, y + (w->h - MW_FH(w)) / 2, "\xbb", side > 0 ? w->atint : w->tint, w->font, 0);
			MR_DrawString(x + (w->w - MW_SW(CText_Value(w), w->font)) / 2, y + (w->h - MW_FH(w)) / 2,
					CText_Value(w), w->tint, w->font, 0);
			return;
		}

		MR_DrawString(x + (w->w - MW_SW(CText_Value(w), w->font)) / 2, y, CText_Value(w), w->tint, w->font, 0);
		return;
	}

	MW_LabelValue(pg, w, x, y, d->atext ? d->atext : "", CText_Value(w), false, false);
}

static qboolean
CText_Key(mpage_t *pg, mwidget_t *w, int key)
{
	ctext_t *d = w->data;

	if (d->prev || d->next)
	{
		int dir = 0;

		if (key == K_LEFTARROW || key == K_MOUSE2)
		{
			dir = -1;
		}
		else if (key == K_RIGHTARROW)
		{
			dir = 1;
		}
		else if (key == K_MOUSE1)
		{
			float mx, my;
			int sx, sy;

			ML_WidgetScreenPos(pg, w, &sx, &sy);
			ML_PageMouse(pg, w, &mx, &my);
			dir = ((int)mx - sx < w->w / 2) ? -1 : 1;
		}

		if (dir && (dir < 0 ? d->prev : d->next))
		{
			M_PlaySound("menu/small/select.wav");
			M_QueueCommand(dir < 0 ? d->prev : d->next, false);
		}

		return false;
	}

	return CText_Value(w)[0] != 0;
}

static void
CText_Free(mwidget_t *w)
{
	ctext_t *d = w->data;

	if (d)
	{
		if (d->atext)
		{
			Z_Free(d->atext);
		}

		if (d->prev)
		{
			Z_Free(d->prev);
		}

		if (d->next)
		{
			Z_Free(d->next);
		}

		Z_Free(d);
	}
}

static const mwclass_t mw_ctext = {
	"ctext", CText_Option, CText_Finish, NULL, NULL, CText_Update, CText_Draw, CText_Key, NULL, CText_Free
};

static void
Tag_CText(mparse_t *p)
{
	char a[1][MENU_MAX_TOKEN];
	mwidget_t *w;

	MP_Args(p, a, 1);
	w = ML_NewWidget(p, &mw_ctext, true);
	w->data = Z_Malloc(sizeof(ctext_t));
	w->cvar = Cvar_GetModule(a[0], "", 0, NULL);

	if (!w->cvar && !p->dry)
	{
		p->page->failed = true;
	}

	w->w = MW_SW(CText_Value(w), w->font) + MW_FH(w) / 2;
	w->h = w->minh = MW_FH(w);
	ML_ReadOptions(p, w);
	ML_AddWidget(p, w);
}

/* ------------------------------------------------------------------ */
/* list                                                                 */

typedef struct
{
	char **labels;
	int numlabels;
	char **match;
	int nummatch;
	int bitmask;        /* -1 none */
	char *atext;
	int index;
	qboolean applied;
	qboolean fit; /* OpenMerc MENU2: labels shortened to the page width (snddevices) */
} list_t;

static int
List_Read(mwidget_t *w)
{
	list_t *d = w->data;
	int i;

	if (!w->cvar)
	{
		return d->index;
	}

	if (d->bitmask >= 0)
	{
		return ((int)w->cvar->value & (1 << d->bitmask)) ? 1 : 0;
	}

	if (!d->match)
	{
		i = (int)w->cvar->value;

		if (i < 0)
		{
			i = 0;
		}

		if (i > d->numlabels - 1)
		{
			i = d->numlabels - 1;
		}

		return i;
	}

	for (i = 0; i < d->nummatch; i++)
	{
		if (!Q_stricmp(d->match[i], w->cvar->string))
		{
			return i;
		}
	}

	return 0;
}

static void
List_Apply(mpage_t *pg, mwidget_t *w, int i)
{
	list_t *d = w->data;
	int A;

	d->index = i;

	if (w->cvar)
	{
		if (d->bitmask >= 0)
		{
			int v = (int)w->cvar->value;

			v = i ? (v | (1 << d->bitmask)) : (v & ~(1 << d->bitmask));
			MW_SetCvarValue(w->cvar, (float)v);
		}
		else if (!d->match)
		{
			MW_SetCvarValue(w->cvar, (float)i);
		}
		else if (i >= 0 && i < d->nummatch)
		{
			MW_SetCvar(w->cvar, d->match[i]);
		}
	}

	/* after an apply the width follows the shown label */
	A = (w->flags & WF_TAB) ? MW_TabStop(pg) : MW_SW(d->atext ? d->atext : "", w->font);
	w->w = A + ((i >= 0 && i < d->numlabels) ? MW_SW(d->labels[i], w->font) : 0);

	if (w->split > 0)
	{
		w->w = 2 * w->split;
	}

	d->applied = true;

	if (w->align == MA_CENTER || w->align == MA_RIGHT)
	{
		ML_Reposition(pg, w);
	}
}

/* upper-cases a whole name */
void
MW_Upper(char *s)
{
	for (; *s; s++)
	{
		if (*s >= 'a' && *s <= 'z')
		{
			*s -= 'a' - 'A';
		}
	}
}

/* SPEC-AMEND-005 I6m.3: the list widgets do not convert listed names; their case comes from the file listing
   (SPEC-300): archive entries are upper case (normalised when the archive is mounted), a loose file has the
   directory part of the requested pattern (normalised to upper case) and the file-name part of the host file
   system. 'name' is relative to the listed directory; 'paks' holds the archive entries of the same listing. */
void
MW_ListedCase(char *name, const char *full, const strlist_t *paks)
{
	char *slash;
	char c;
	int i;

	for (i = 0; i < paks->num; i++)
	{
		if (!strcmp(paks->data[i], full))
		{
			MW_Upper(name);
			return;
		}
	}

	slash = strrchr(name, '/');

	if (slash)
	{
		c = slash[1];
		slash[1] = 0;
		MW_Upper(name);
		slash[1] = c;
	}
}

static void
List_Files(list_t *d, const char *dir, const char *pattern, const char *ext)
{
	strlist_t files, paks;
	char find[MAX_QPATH * 2];
	int i, j, n = 0;
	qboolean strip = ext[0] && ext[0] != '.' && !strchr(ext, '*');
	size_t dl = strlen(dir);

	MW_FreeList(d->labels, d->numlabels);
	MW_FreeList(d->match, d->nummatch);
	d->labels = d->match = NULL;
	d->numlabels = d->nummatch = 0;

	if (ext[0])
	{
		Com_sprintf(find, sizeof(find), "%s/%s.%s", dir, pattern, ext);
	}
	else
	{
		Com_sprintf(find, sizeof(find), "%s/%s", dir, pattern);
	}

	files = FS_ListFiles2(find, 0, SFF_SUBDIR);

	if (files.num <= 0)
	{
		StrList_Free(&files);
		return;
	}

	paks = FS_ListFiles2(find, SFF_INPACK, SFF_SUBDIR);
	d->labels = Z_Malloc(files.num * sizeof(char *));
	d->match = Z_Malloc(files.num * sizeof(char *));

	for (i = 0; i < files.num; i++)
	{
		char name[MAX_QPATH];
		const char *s = files.data[i];

		if (!Q_strncasecmp(s, dir, (int)dl) && s[dl] == '/')
		{
			s += dl + 1;
		}

		Q_strlcpy(name, s, sizeof(name));
		MW_ListedCase(name, files.data[i], &paks);

		if (strip)
		{
			char *dot = strrchr(name, '.');

			if (dot)
			{
				*dot = 0;
			}
		}

		for (j = 0; j < n; j++)
		{
			if (!Q_stricmp(d->labels[j], name))
			{
				break;
			}
		}

		if (j < n)
		{
			continue;
		}

		d->labels[n] = CopyString(name);
		d->match[n] = CopyString(name);
		n++;
	}

	d->numlabels = d->nummatch = n;
	StrList_Free(&files);
	StrList_Free(&paks);
}

static void
List_VidModes(list_t *d)
{
	int ws[64], hs[64], n, i, k;
	char **labels, **match;

	n = GLimp_GetDisplayModes(ws, hs, 64);

	if (n < 0)
	{
		n = 0;
	}

	/* MENU-SCRATCH: a window size that is no display mode (a 1440x1080 window
	   on a 1920x1080 display) is listed too, in size order; without it the row
	   matched nothing and showed the first label, "Desktop" */
	if ((int)Cvar_VariableValue("r_mode") != -2 && viddef.width > 0 && viddef.height > 0 && n < 64)
	{
		int cur = viddef.width * viddef.height, at = n;
		qboolean down = n > 1 && ws[0] * hs[0] > ws[n - 1] * hs[n - 1];

		for (k = 0; k < n; k++)
		{
			if (ws[k] == viddef.width && hs[k] == viddef.height)
			{
				break;
			}
		}

		if (k == n)
		{
			for (k = 0; k < n; k++)
			{
				int a = ws[k] * hs[k];

				if (down ? (a < cur) : (a > cur))
				{
					at = k;
					break;
				}
			}

			for (k = n; k > at; k--)
			{
				ws[k] = ws[k - 1];
				hs[k] = hs[k - 1];
			}

			ws[at] = viddef.width;
			hs[at] = viddef.height;
			n++;
		}
	}

	if (n <= 0)
	{
		return;
	}

	labels = Z_Malloc((d->numlabels + n) * sizeof(char *));
	match = Z_Malloc((d->nummatch + n) * sizeof(char *));

	for (i = 0; i < d->numlabels; i++)
	{
		labels[i] = d->labels[i];
	}

	for (i = 0; i < d->nummatch; i++)
	{
		match[i] = d->match[i];
	}

	for (k = 0; k < n; k++)
	{
		const char *s = va("%dx%d", ws[k], hs[k]);

		labels[d->numlabels + k] = CopyString(s);
		match[d->nummatch + k] = CopyString(s);
	}

	if (d->labels)
	{
		Z_Free(d->labels);
	}

	if (d->match)
	{
		Z_Free(d->match);
	}

	d->labels = labels;
	d->match = match;
	d->numlabels += n;
	d->nummatch += n;
}

static void
List_SndDevices(list_t *d)
{
	static char names[4096], labs[4096];
	char **labels, **match;
	const char *s, *t;
	int n, i, k;

	n = Sys_SoundDevices(names, labs, sizeof(names));

	if (n <= 0)
	{
		return;
	}

	labels = Z_Malloc((d->numlabels + n) * sizeof(char *));
	match = Z_Malloc((d->nummatch + n) * sizeof(char *));

	for (i = 0; i < d->numlabels; i++)
	{
		labels[i] = d->labels[i];
	}

	for (i = 0; i < d->nummatch; i++)
	{
		match[i] = d->match[i];
	}

	for (k = 0, s = names, t = labs; k < n; k++, s += strlen(s) + 1, t += strlen(t) + 1)
	{
		const char *l = t;

		/* OpenAL Soft names its outputs "OpenAL Soft on <Windows name>" */
		if (!Q_strncasecmp(l, "OpenAL Soft on ", 15))
		{
			l += 15;
		}

		labels[d->numlabels + k] = CopyString(l);
		match[d->nummatch + k] = CopyString(s);
	}

	if (d->labels)
	{
		Z_Free(d->labels);
	}

	if (d->match)
	{
		Z_Free(d->match);
	}

	d->labels = labels;
	d->match = match;
	d->numlabels += n;
	d->nummatch += n;
	d->fit = true;
}

static qboolean
List_Option(mparse_t *p, mwidget_t *w, const char *kw)
{
	list_t *d = w->data;
	char a[3][MENU_MAX_TOKEN];

	if (!Q_stricmp(kw, "atext"))
	{
		MP_Args(p, a, 1);

		if (d->atext)
		{
			Z_Free(d->atext);
		}

		d->atext = CopyString(a[0]);
	}
	else if (!Q_stricmp(kw, "match"))
	{
		MP_Args(p, a, 1);
		MW_FreeList(d->match, d->nummatch);
		MW_SplitComma(a[0], &d->match, &d->nummatch);

		if (d->nummatch != d->numlabels)
		{
			MP_Error(p, "Different number of match parameters to list parameters");
		}
	}
	else if (!Q_stricmp(kw, "bitmask"))
	{
		MP_Args(p, a, 1);
		d->bitmask = atoi(a[0]);
	}
	else if (!Q_stricmp(kw, "files"))
	{
		MP_Args(p, a, 3);
		List_Files(d, a[0], a[1], a[2]);
	}
	else if (!Q_stricmp(kw, "vidmodes"))
	{
		/* OpenMerc MENU2 (docs/DEVIATIONS.md "Resolution list"): the display's
		   fullscreen sizes are appended as "WxH" labels and matches */
		List_VidModes(d);
	}
	else if (!Q_stricmp(kw, "snddevices"))
	{
		/* OpenMerc MENU2 (docs/DEVIATIONS.md "Audio output device"): the OpenAL outputs, label in the
		   menu charset without the "OpenAL Soft on " prefix, match the full UTF-8 name for s_om_device */
		List_SndDevices(d);
	}
	else
	{
		return false;
	}

	return true;
}

static qboolean
List_Finish(mparse_t *p, mwidget_t *w)
{
	list_t *d = w->data;
	int A, i, widest = 0;

	if (d->numlabels <= 0)
	{
		return false;
	}

	A = (w->flags & WF_TAB) ? MW_TabStop(p->page) : MW_SW(d->atext ? d->atext : "", w->font);

	if (d->fit)
	{
		/* a device name wider than the page would drop the whole row ("Object too wide to place"):
		   cut it and end it with "..." */
		int maxw = ML_ContentW(p->page) - 2 * w->bpad - A;

		for (i = 0; i < d->numlabels; i++)
		{
			char cut[MENU_MAX_TOKEN];
			int len;

			if (MW_SW(d->labels[i], w->font) <= maxw)
			{
				continue;
			}

			Q_strlcpy(cut, d->labels[i], sizeof(cut) - 4);
			len = (int)strlen(cut);

			while (len > 1)
			{
				cut[--len] = 0;
				Q_strlcpy(cut + len, "...", sizeof(cut) - len);

				if (MW_SW(cut, w->font) <= maxw)
				{
					break;
				}

				cut[len] = 0;
			}

			Z_Free(d->labels[i]);
			d->labels[i] = CopyString(cut);
		}
	}

	for (i = 0; i < d->numlabels; i++)
	{
		int sw = MW_SW(d->labels[i], w->font);

		if (sw > widest)
		{
			widest = sw;
		}
	}

	w->w = (w->split > 0) ? MW_SplitWidth(w, A, widest, ML_ContentW(p->page) - 2 * w->bpad) : A + widest;
	w->h = MW_FH(w);
	w->flags |= WF_INTERACTIVE;
	return true;
}

static qboolean
List_EndPage(mpage_t *pg, mwidget_t *w)
{
	if (!pg->dry)
	{
		List_Apply(pg, w, List_Read(w));
	}

	return true;
}

static void
List_Refresh(mpage_t *pg, mwidget_t *w)
{
	List_Apply(pg, w, List_Read(w));
}

static void
List_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	list_t *d = w->data;
	int i = d->applied ? d->index : List_Read(w);
	const char *value = (i >= 0 && i < d->numlabels) ? d->labels[i] : "";

	if (d->applied && w->cvar)
	{
		i = List_Read(w);
		value = (i >= 0 && i < d->numlabels) ? d->labels[i] : "";
	}

	MW_LabelValue(pg, w, x, y, d->atext ? d->atext : "", value, false, MW_Disabled(w));
}

static qboolean
List_Key(mpage_t *pg, mwidget_t *w, int key)
{
	list_t *d = w->data;
	int old, i;

	if (MW_Disabled(w))
	{
		return false;
	}

	/* MENU-SCRATCH: the arrow keys (and the d-pad, which the menus map to them)
	   step a list like the mouse buttons, so lists work without a mouse */
	if (key == K_MOUSE1 || key == K_MOUSE2 || key == K_RIGHTARROW || key == K_LEFTARROW)
	{
		old = List_Read(w);
		i = old + ((key == K_MOUSE1 || key == K_RIGHTARROW) ? 1 : -1);

		if (i >= d->numlabels)
		{
			i = 0;
		}

		if (i < 0)
		{
			i = d->numlabels - 1;
		}

		List_Apply(pg, w, i);
		i = List_Read(w);
		d->index = i;
		M_PlaySound(i == old ? "menu/small/invalid.wav" : "menu/small/select.wav");
	}

	return true;
}

static void
List_Free(mwidget_t *w)
{
	list_t *d = w->data;

	if (d)
	{
		MW_FreeList(d->labels, d->numlabels);
		MW_FreeList(d->match, d->nummatch);

		if (d->atext)
		{
			Z_Free(d->atext);
		}

		Z_Free(d);
	}
}

static const mwclass_t mw_list = {
	"list", List_Option, List_Finish, List_EndPage, List_Refresh, NULL, List_Draw, List_Key, NULL, List_Free
};

static void
Tag_List(mparse_t *p)
{
	char a[1][MENU_MAX_TOKEN];
	mwidget_t *w;
	list_t *d;

	MP_Args(p, a, 1);
	w = ML_NewWidget(p, &mw_list, true);
	d = w->data = Z_Malloc(sizeof(list_t));
	d->bitmask = -1;
	MW_SplitComma(a[0], &d->labels, &d->numlabels);
	ML_ReadOptions(p, w);
	ML_AddWidget(p, w);
}

/* ------------------------------------------------------------------ */
/* slider                                                               */

typedef struct
{
	float min, max, step;
	qboolean hasstep, vertical;
	char bar[MAX_QPATH], button[MAX_QPATH], cap[MAX_QPATH];
	qboolean setbar, setbutton, setcap;
	qboolean flat;	/* MENU-SCRATCH: own flat drawing (wide layout), no stock pictures */
	int bw, bh, kw, kh, cw, ch;
	char atext[128];	/* MENU-SCRATCH: label of a 'split' row (left column) */
} slider_t;

/* MENU-SCRATCH: x of the bar inside a 'split' slider row */
static int
Slider_BarX(const mwidget_t *w)
{
	const slider_t *d = w->data;
	return (d->flat && w->split > 0) ? w->split + MW_SplitGap(w) : 0;
}

static float
Slider_Value(mwidget_t *w)
{
	slider_t *d = w->data;
	float v = w->cvar ? w->cvar->value : 0;

	if (v < d->min)
	{
		v = d->min;
	}

	if (v > d->max)
	{
		v = d->max;
	}

	return v;
}

static qboolean
Slider_Option(mparse_t *p, mwidget_t *w, const char *kw)
{
	slider_t *d = w->data;
	char a[1][MENU_MAX_TOKEN];

	if (!Q_stricmp(kw, "min"))
	{
		MP_Args(p, a, 1);
		d->min = (float)atof(a[0]);
	}
	else if (!Q_stricmp(kw, "max"))
	{
		MP_Args(p, a, 1);
		d->max = (float)atof(a[0]);
	}
	else if (!Q_stricmp(kw, "cvarmax"))
	{
		MP_Args(p, a, 1);
		d->max = Cvar_VariableValue(a[0]);
	}
	else if (!Q_stricmp(kw, "step"))
	{
		MP_Args(p, a, 1);
		d->step = (float)atof(a[0]);
		d->hasstep = true;
	}
	else if (!Q_stricmp(kw, "vertical"))
	{
		d->vertical = true;
	}
	else if (!Q_stricmp(kw, "atext"))
	{
		MP_Args(p, a, 1);
		Q_strlcpy(d->atext, a[0], sizeof(d->atext));
	}
	else if (!Q_stricmp(kw, "horizontal"))
	{
		d->vertical = false;
	}
	else if (!Q_stricmp(kw, "bar"))
	{
		MP_Args(p, a, 1);
		Q_strlcpy(d->bar, PIC(a[0]), sizeof(d->bar));
		d->setbar = true;
	}
	else if (!Q_stricmp(kw, "button"))
	{
		MP_Args(p, a, 1);
		Q_strlcpy(d->button, PIC(a[0]), sizeof(d->button));
		d->setbutton = true;
	}
	else if (!Q_stricmp(kw, "cap"))
	{
		MP_Args(p, a, 1);
		Q_strlcpy(d->cap, PIC(a[0]), sizeof(d->cap));
		d->setcap = true;
	}
	else
	{
		return false;
	}

	return true;
}

static qboolean
Slider_Finish(mparse_t *p, mwidget_t *w)
{
	slider_t *d = w->data;

	if (!d->hasstep)
	{
		d->step = (d->max - d->min > 2) ? 1.0f : 0.05f;
	}

	/* MENU-SCRATCH (owner t95u): a default horizontal slider of the wide
	   layout is drawn flat (own art): track, fill and knob */
	if (MWD_Enabled() && !d->vertical && !d->setbar && !d->setbutton && !d->setcap)
	{
		d->flat = true;
		d->bw = 150;
		d->bh = 14;
		d->kw = 8;
		d->kh = 14;
		d->cw = 4;
		d->ch = 4;
		w->w = d->bw + 2 * d->ch;
		w->h = d->bh;

		if (w->split > 0)
		{
			w->w = MW_SplitWidth(w, MW_SW(d->atext, w->font), w->w, ML_ContentW(p->page) - 2 * w->bpad);
			w->h = MW_FH(w) > d->bh ? MW_FH(w) : d->bh;
		}

		w->flags |= WF_INTERACTIVE;
		return true;
	}

	if (!d->setbar)
	{
		Q_strlcpy(d->bar, PIC(d->vertical ? "misc/barv" : "misc/bar"), sizeof(d->bar));
	}

	if (!d->setbutton)
	{
		Q_strlcpy(d->button, PIC(d->vertical ? "misc/buttonv" : "misc/button"), sizeof(d->button));
	}

	if (!d->setcap)
	{
		Q_strlcpy(d->cap, PIC(d->vertical ? "misc/capv" : "misc/cap"), sizeof(d->cap));
	}

	if (!d->hasstep)
	{
		d->step = (d->max - d->min > 2) ? 1.0f : 0.05f;
	}

	MR_GetPicSize(&d->bw, &d->bh, d->bar);
	MR_GetPicSize(&d->kw, &d->kh, d->button);
	MR_GetPicSize(&d->cw, &d->ch, d->cap);

	if (d->vertical)
	{
		w->w = d->bw;
		w->h = d->bh + 2 * d->ch;
	}
	else
	{
		/* the cap height, faithful to the original */
		w->w = d->bw + 2 * d->ch;
		w->h = d->bh;
	}

	w->flags |= WF_INTERACTIVE;
	return true;
}

static void
Slider_Store(mwidget_t *w, float v)
{
	slider_t *d = w->data;

	if (v < d->min)
	{
		v = d->min;
	}

	if (v > d->max)
	{
		v = d->max;
	}

	MW_SetCvarValue(w->cvar, v);
}

static qboolean
Slider_EndPage(mpage_t *pg, mwidget_t *w)
{
	slider_t *d = w->data;

	if (d->min > d->max)
	{
		mparse_t dummy;

		memset(&dummy, 0, sizeof(dummy));
		Q_strlcpy(dummy.tag, "slider", sizeof(dummy.tag));
		MP_Error(&dummy, "Cleanup : min > max");
		return false;
	}

	if (!pg->dry)
	{
		Slider_Store(w, Slider_Value(w));
	}

	return true;
}

static void
Slider_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	slider_t *d = w->data;
	float v = Slider_Value(w), range = d->max - d->min;
	mcolor_t bc = MW_Disabled(w) ? ((w->tint & 0x00ffffff) | 0x60000000) : w->tint;

	(void)pg;

	if (range == 0)
	{
		range = 1;
	}

	if (d->flat && w->split > 0)
	{
		mcolor_t lc = MW_Disabled(w) ? ((w->tint & 0x00ffffff) | 0x60000000) : MW_Shade(w->tint);

		if (w->flags & WF_HOVER)
		{
			MR_Fill(x, y, w->w, w->h, w->btint);
		}

		MR_DrawString(x + w->split - MW_SplitGap(w) - MW_SW(d->atext, w->font), y + (w->h - MW_FH(w)) / 2,
				d->atext, lc, w->font, 0);
		x += Slider_BarX(w);
		y += (w->h - d->bh) / 2;
	}

	if (d->flat)
	{
		int kx = (int)floorf((v - d->min) * (d->bw - d->kw - 2) / range + d->cw + x) + 1;
		qboolean hot = (w->flags & WF_HOVER) != 0;

		MR_FlatBox(x, y + d->bh / 2 - 2, d->bw + 2 * d->cw, 4, 0xFF2A231C, 0xFF54473B, 0, false);
		MR_FlatBox(x + 1, y + d->bh / 2 - 1, kx - x, 2, MW_Disabled(w) ? 0x80808080 : 0xFF2080FF, 0, 0, false);
		MR_FlatBox(kx, y + (d->bh - d->kh) / 2, d->kw, d->kh,
			MW_Disabled(w) ? 0x80A0A0A0 : (hot ? 0xFFFFFFFF : 0xFFD8DCD0), 0xFF100D0A, 0, false);
		return;
	}

	if (!d->vertical)
	{
		MR_StretchPic(x, y, d->cw, d->ch, w->tint, d->cap, 0);
		MR_StretchPic(x + d->cw, y, d->bw, d->bh, w->tint, d->bar, 0);
		MR_StretchPic(x + d->cw + d->bw, y, d->cw, d->ch, w->tint, d->cap, 5);
		MR_StretchPic((int)floorf((v - d->min) * (d->bw - d->kw - 2) / range + d->cw + x) + 1,
				y + (int)floorf((d->bh - d->kh) / 2.0f), d->kw, d->kh, bc, d->button, 0);
	}
	else
	{
		MR_StretchPic(x, y, d->cw, d->ch, w->tint, d->cap, 0);
		MR_StretchPic(x, y + d->ch, d->bw, d->bh, w->tint, d->bar, 0);
		MR_StretchPic(x, y + d->ch + d->bh, d->cw, d->ch, w->tint, d->cap, 6);
		MR_StretchPic(x + (int)floorf((d->bw - d->kw) / 2.0f),
				(int)floorf((d->max - (v - d->min)) * (d->bh - d->kh - 2) / range + d->ch + y) + 1,
				d->kw, d->kh, bc, d->button, 0);
	}
}

static qboolean
Slider_Key(mpage_t *pg, mwidget_t *w, int key)
{
	slider_t *d = w->data;
	float v = w->cvar ? w->cvar->value : 0;
	qboolean changed = false;

	(void)pg;

	if (MW_Disabled(w))
	{
		return false;
	}

	if (!d->vertical)
	{
		if (key == K_LEFTARROW)
		{
			v -= d->step;
			changed = true;
		}
		else if (key == K_RIGHTARROW)
		{
			v += d->step;
			changed = true;
		}
	}
	else
	{
		if (key == K_UPARROW)
		{
			v += d->step;
			changed = true;
		}
		else if (key == K_DOWNARROW)
		{
			v -= d->step;
			changed = true;
		}
	}

	if (changed)
	{
		M_PlaySound("menu/small/select.wav");
	}

	Slider_Store(w, v);
	return true;
}

static void
Slider_Activate(mpage_t *pg, mwidget_t *w, int button, int mx, int my)
{
	slider_t *d = w->data;
	float v = w->cvar ? w->cvar->value : 0;

	if (button != 1 || MW_Disabled(w))
	{
		return;
	}

	pg->drag = w;
	mx -= Slider_BarX(w);

	if (!d->vertical)
	{
		if (mx < d->cw)
		{
			v -= d->step;
		}
		else if (mx > d->cw + d->bw)
		{
			v += d->step;
		}
		else if (d->bw - d->kw > 0)
		{
			int pp = mx - d->kw / 2 - d->cw;

			if (pp < 0) pp = 0;
			if (pp > d->bw - d->kw) pp = d->bw - d->kw;

			v = d->min + (d->max - d->min) * pp / (d->bw - d->kw);
		}
	}
	else
	{
		int H = d->bh + 2 * d->ch;

		if (my < d->ch)
		{
			v += d->step;
		}
		else if (my > d->ch + d->bh)
		{
			v -= d->step;
		}
		else if (d->bh - d->kh > 0)
		{
			int pp = d->kh / 2 + H - d->kh - d->ch - my;

			if (pp < 0) pp = 0;
			if (pp > d->bh - d->kh) pp = d->bh - d->kh;

			v = d->min + (d->max - d->min) * pp / (d->bh - d->kh);
		}
	}

	Slider_Store(w, v);
}

static const mwclass_t mw_slider = {
	"slider", Slider_Option, Slider_Finish, Slider_EndPage, NULL, NULL, Slider_Draw, Slider_Key,
	Slider_Activate, NULL
};

static void
Tag_Slider(mparse_t *p)
{
	mwidget_t *w = ML_NewWidget(p, &mw_slider, false);
	slider_t *d = w->data = Z_Malloc(sizeof(slider_t));

	d->min = 0;
	d->max = 1;
	ML_ReadOptions(p, w);
	ML_AddWidget(p, w);
}

/* ------------------------------------------------------------------ */
/* input                                                                */

typedef struct
{
	int maxchars;
	qboolean history;
	char last[256];      /* value written last */
	char **lines;
	int numlines, pos;
} input_t;

static void
Input_Fit(mwidget_t *w, char *s)
{
	input_t *d = w->data;
	int room = w->w - MW_SW("_", w->font);

	if (d->maxchars > 0)
	{
		if ((int)strlen(s) > d->maxchars)
		{
			memmove(s + d->maxchars, s + d->maxchars + 1, strlen(s + d->maxchars + 1) + 1);
		}

		return;
	}

	if (w->flags & WF_HIDDEN)
	{
		while (s[0] && (int)strlen(s) * MW_SW("*", w->font) > room)
		{
			s[strlen(s) - 1] = 0;
		}
	}
	else
	{
		while (s[0] && MW_SW(s, w->font) > room)
		{
			s[strlen(s) - 1] = 0;
		}
	}
}

static void
Input_Write(mwidget_t *w, const char *s)
{
	input_t *d = w->data;

	Q_strlcpy(d->last, s, sizeof(d->last));
	MW_SetCvar(w->cvar, s);
}

static qboolean
Input_Option(mparse_t *p, mwidget_t *w, const char *kw)
{
	input_t *d = w->data;
	char a[1][MENU_MAX_TOKEN];

	if (!Q_stricmp(kw, "global"))
	{
		w->flags |= WF_GLOBAL;
	}
	else if (!Q_stricmp(kw, "hidden"))
	{
		w->flags |= WF_HIDDEN;
	}
	else if (!Q_stricmp(kw, "maxchars"))
	{
		MP_Args(p, a, 1);
		d->maxchars = atoi(a[0]);
	}
	else if (!Q_stricmp(kw, "history"))
	{
		d->history = true;
	}
	else
	{
		return false;
	}

	return true;
}

static qboolean
Input_Finish(mparse_t *p, mwidget_t *w)
{
	w->h = MW_FH(w);
	w->flags |= WF_INTERACTIVE;

	if (w->flags & WF_GLOBAL)
	{
		p->page->focus = w;
	}

	return true;
}

static qboolean
Input_EndPage(mpage_t *pg, mwidget_t *w)
{
	char s[256];

	if (pg->dry || !w->cvar)
	{
		return true;
	}

	Q_strlcpy(s, w->cvar->string, sizeof(s));
	Input_Fit(w, s);
	Input_Write(w, s);
	return true;
}

static void
Input_Update(mpage_t *pg, mwidget_t *w)
{
	input_t *d = w->data;

	(void)pg;

	if (!w->cvar || !strcmp(w->cvar->string, d->last))
	{
		return;
	}

	/* changed by other means (a command cleared it after sending) */
	if (d->last[0])
	{
		char **nl = Z_Malloc((d->numlines + 1) * sizeof(char *));

		if (d->lines)
		{
			memcpy(nl, d->lines, d->numlines * sizeof(char *));
			Z_Free(d->lines);
		}

		nl[d->numlines++] = CopyString(d->last);
		d->lines = nl;
	}

	d->pos = d->numlines;
	Q_strlcpy(d->last, w->cvar->string, sizeof(d->last));
}

static void
Input_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	char shown[256];
	const char *s = w->cvar ? w->cvar->string : "";
	qboolean active = (w->flags & WF_GLOBAL) || MW_Hovered(w);
	int tw, room = w->w - MW_SW("_", w->font), P;

	(void)pg;

	if (active)
	{
		MR_Fill(x, y, w->w, w->h, w->btint);
	}

	if (w->flags & WF_HIDDEN)
	{
		int n = (int)strlen(s), i;

		if (n > 255)
		{
			n = 255;
		}

		for (i = 0; i < n; i++)
		{
			shown[i] = '*';
		}

		shown[n] = 0;
	}
	else
	{
		Q_strlcpy(shown, s, sizeof(shown));
	}

	tw = MW_SW(shown, w->font);
	MR_ClipLeft(x);
	/* SPEC-AMEND-005 I6m.1: the value, the '*' characters and the '_' cursor are always drawn in tint; atint is
	   never used by input */
	MR_DrawString(tw > room ? x - (tw - room) : x, y, shown, w->tint, w->font, 0);

	P = (int)cursor_blink_speed->value;

	if (active && P > 0 && (M_Time() % P) > P / 2)
	{
		MR_DrawString((tw > room ? x - (tw - room) : x) + tw, y, "_", w->tint, w->font, 0);
	}

	MR_Reclip();
}

static qboolean
Input_Key(mpage_t *pg, mwidget_t *w, int key)
{
	input_t *d = w->data;
	char s[512];
	qboolean shift = keydown[K_LSHIFT] || keydown[K_RSHIFT];
	qboolean ctrl = keydown[K_LCTRL] || keydown[K_RCTRL];

	(void)pg;

	if (!w->cvar)
	{
		return true;
	}

	Q_strlcpy(s, w->cvar->string, sizeof(s));

	if (Key_Flags(key) & KEYF_CLICK)
	{
		M_PlaySound("menu/small/click.wav");
	}

	if (key == K_BACKSPACE || key == K_DEL)
	{
		if (s[0])
		{
			s[strlen(s) - 1] = 0;
		}
	}
	else if (key == K_ENTER || key == K_KP_ENTER || key == K_ESCAPE)
	{
	}
	else if ((key == K_UPARROW || key == K_DOWNARROW) && d->history)
	{
		if (d->numlines > 0)
		{
			d->pos += (key == K_UPARROW) ? -1 : 1;

			if (d->pos < 0)
			{
				d->pos = d->numlines - 1;
			}

			if (d->pos >= d->numlines)
			{
				d->pos = 0;
			}

			Q_strlcpy(s, d->lines[d->pos], sizeof(s));
		}
	}
	else if ((shift && (key == K_INS || key == K_KP_INS)) || (ctrl && key == 'V'))
	{
		char clip[256];
		char *e;

		clip[0] = 0;
		IN_GetClipboardText(clip, sizeof(clip));

		for (e = clip; *e && *e != '\r' && *e != '\n' && *e != 8; e++)
		{
		}

		*e = 0;
		Q_strlcat(s, clip, sizeof(s));
	}
	else
	{
		int c = Key_Char(key, shift);

		if (c >= 32 && c < 256)
		{
			size_t l = strlen(s);

			if (l < sizeof(s) - 1)
			{
				s[l] = (char)c;
				s[l + 1] = 0;
			}
		}
	}

	s[255] = 0;
	Input_Fit(w, s);
	Input_Write(w, s);
	return true;
}

static void
Input_Free(mwidget_t *w)
{
	input_t *d = w->data;

	if (d)
	{
		MW_FreeList(d->lines, d->numlines);
		Z_Free(d);
	}
}

static const mwclass_t mw_input = {
	"input", Input_Option, Input_Finish, Input_EndPage, NULL, Input_Update, Input_Draw, Input_Key, NULL,
	Input_Free
};

static void
Tag_Input(mparse_t *p)
{
	/* SPEC-AMEND-005 I6m.1: only the page font's NAME is taken; tint and atint start opaque white, btint
	   transparent black, changed only by the tag's own options */
	mwidget_t *w = ML_NewWidget(p, &mw_input, false);

	w->data = Z_Malloc(sizeof(input_t));
	w->h = w->minh = MW_FH(w);
	ML_ReadOptions(p, w);
	ML_AddWidget(p, w);
}

/* ------------------------------------------------------------------ */
/* setkey                                                               */

typedef struct
{
	char *atext;
	char keytext[128];
	qboolean drawn;
} setkey_t;

static void
SetKey_Text(mwidget_t *w)
{
	setkey_t *d = w->data;
	int k, n = 0;

	d->keytext[0] = 0;

	for (k = 0; k < K_LAST && n < 2; k++)
	{
		if (keybindings[k] && keybindings[k][0] && !Q_stricmp(keybindings[k], w->text))
		{
			const char *name = MSP_Find(va("KEY_%s", Key_KeynumToString(k)));

			/* MENU-SCRATCH: no key-name package entry: the engine's own name */
			if (!name[0])
			{
				name = Key_KeynumToString(k);
			}

			if (n)
			{
				Q_strlcat(d->keytext, " / ", sizeof(d->keytext));
			}

			Q_strlcat(d->keytext, name, sizeof(d->keytext));
			n++;
		}
	}

	if (!n)
	{
		/* MENU-SCRATCH: own wording for an unbound action instead of "???" */
		const char *none = MSP_Find("MENU_OMSET_PG_K_NONE");

		Q_strlcpy(d->keytext, none[0] ? none : "-", sizeof(d->keytext));
	}
}

static qboolean
SetKey_Option(mparse_t *p, mwidget_t *w, const char *kw)
{
	setkey_t *d = w->data;
	char a[1][MENU_MAX_TOKEN];

	if (!Q_stricmp(kw, "atext"))
	{
		MP_Args(p, a, 1);

		if (d->atext)
		{
			Z_Free(d->atext);
		}

		d->atext = CopyString(a[0]);
		return true;
	}

	if (!Q_stricmp(kw, "talign"))
	{
		MP_Args(p, a, 1);

		if (Q_stricmp(a[0], "left") && Q_stricmp(a[0], "right") && Q_stricmp(a[0], "center"))
		{
			MP_Error(p, "Invalid align parameter");
		}

		return true;
	}

	return false;
}

static qboolean
SetKey_Finish(mparse_t *p, mwidget_t *w)
{
	setkey_t *d = w->data;

	(void)p;
	SetKey_Text(w);

	if (w->split > 0)
	{
		/* MENU-SCRATCH: two columns, action right-aligned left of the middle */
		w->w = MW_SplitWidth(w, MW_SW(d->atext ? d->atext : "", w->font),
			MW_SW("MOUSE2 / MWHEELDOWN", w->font), ML_ContentW(p->page) - 2 * w->bpad);
	}
	else
	{
		w->w = MW_SW(d->keytext, w->font) + MW_SW(d->atext ? d->atext : "", w->font);
	}

	w->h = MW_FH(w);
	w->flags |= WF_INTERACTIVE;
	return true;
}

static void
SetKey_Update(mpage_t *pg, mwidget_t *w)
{
	setkey_t *d = w->data;

	SetKey_Text(w);

	if (d->drawn && (w->flags & WF_TAB) && w->split <= 0)
	{
		w->w = MW_TabStop(pg) + MW_SW(d->keytext, w->font);
	}
}

static void
SetKey_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	setkey_t *d = w->data;

	MW_LabelValue(pg, w, x, y, d->atext ? d->atext : "", d->keytext, ms.capture == w, false);
	d->drawn = true;
}

static void
SetKey_Unbind(mwidget_t *w)
{
	int k;

	for (k = 0; k < K_LAST; k++)
	{
		if (keybindings[k] && keybindings[k][0] && !Q_stricmp(keybindings[k], w->text))
		{
			Key_SetBinding(k, "");
		}
	}
}

static qboolean
SetKey_Key(mpage_t *pg, mwidget_t *w, int key)
{
	(void)pg;

	if (ms.capture == w)
	{
		/* the key the menu received while waiting is bound */
		if (key != K_ESCAPE)
		{
			M_QueueCommand(va("bind \"%s\" \"%s\"", Key_KeynumToString(key), w->text), true);
		}

		ms.capture = NULL;
		return true;
	}

	if (key == K_MOUSE1)
	{
		int k, n = 0;

		for (k = 0; k < K_LAST; k++)
		{
			if (keybindings[k] && keybindings[k][0] && !Q_stricmp(keybindings[k], w->text))
			{
				n++;
			}
		}

		if (n >= 2)
		{
			SetKey_Unbind(w);
		}

		ms.capture = w;
		M_PlaySound("menu/small/select.wav");
	}
	else if (key == K_DEL)
	{
		SetKey_Unbind(w);
	}

	return true;
}

static void
SetKey_Free(mwidget_t *w)
{
	setkey_t *d = w->data;

	if (d)
	{
		if (d->atext)
		{
			Z_Free(d->atext);
		}

		Z_Free(d);
	}
}

static const mwclass_t mw_setkey = {
	"setkey", SetKey_Option, SetKey_Finish, NULL, NULL, SetKey_Update, SetKey_Draw, SetKey_Key, NULL,
	SetKey_Free
};

static void
Tag_SetKey(mparse_t *p)
{
	char a[1][MENU_MAX_TOKEN];
	mwidget_t *w;

	MP_Args(p, a, 1);
	w = ML_NewWidget(p, &mw_setkey, true);
	w->data = Z_Malloc(sizeof(setkey_t));
	w->text = CopyString(a[0]);
	ML_ReadOptions(p, w);
	ML_AddWidget(p, w);
}

/* ------------------------------------------------------------------ */
/* image                                                                */

typedef struct
{
	char pic[MAX_QPATH];
	char alt[MAX_QPATH];
	qboolean hflip, vflip, transient;
	cvar_t *setimage;
	char text[256];
	int tx, ty;
	cvar_t *cvartext;
	int cx, cy;
	mcolor_t texttint;
	char label[64];	/* MENU-SCRATCH: 'label': own tab caption of the wide layout */
} image_t;

static qboolean
Image_Option(mparse_t *p, mwidget_t *w, const char *kw)
{
	image_t *d = w->data;
	char a[3][MENU_MAX_TOKEN];

	if (!Q_stricmp(kw, "label"))
	{
		MP_Args(p, a, 1);
		Q_strlcpy(d->label, a[0], sizeof(d->label));
	}
	else if (!Q_stricmp(kw, "abs"))
	{
		MP_Args(p, a, 2);
		w->x = atoi(a[0]);
		w->y = atoi(a[1]);
		w->flags |= WF_NOTPLACED;
	}
	else if (!Q_stricmp(kw, "alt"))
	{
		MP_Args(p, a, 1);
		Q_strlcpy(d->alt, PIC(a[0]), sizeof(d->alt));
	}
	else if (!Q_stricmp(kw, "hflip"))
	{
		d->hflip = true;
	}
	else if (!Q_stricmp(kw, "vflip"))
	{
		d->vflip = true;
	}
	else if (!Q_stricmp(kw, "scale"))
	{
		MP_Args(p, a, 1);
		w->scale = (float)atof(a[0]);
	}
	else if (!Q_stricmp(kw, "text"))
	{
		MP_Args(p, a, 3);
		Q_strlcpy(d->text, a[0], sizeof(d->text));
		d->tx = atoi(a[1]);
		d->ty = atoi(a[2]);
	}
	else if (!Q_stricmp(kw, "cvartext"))
	{
		MP_Args(p, a, 3);
		d->cvartext = Cvar_Get(a[0], "0", 0);
		d->cx = atoi(a[1]);
		d->cy = atoi(a[2]);
	}
	else if (!Q_stricmp(kw, "setimage"))
	{
		MP_Args(p, a, 1);
		d->setimage = Cvar_Get(a[0], d->pic + strlen("pics/menus/"), 0);
	}
	else if (!Q_stricmp(kw, "transient"))
	{
		d->transient = true;
	}
	else
	{
		return false;
	}

	return true;
}

static qboolean
Image_EndPage(mpage_t *pg, mwidget_t *w)
{
	image_t *d = w->data;

	if (!pg->dry && d->setimage && !d->setimage->string[0])
	{
		Cvar_Set(d->setimage->name, d->pic + strlen("pics/menus/"));
	}

	return true;
}

static void
Image_Update(mpage_t *pg, mwidget_t *w)
{
	image_t *d = w->data;
	cvar_t *g, *c, *b, *m;

	(void)pg;

	if (!d->transient)
	{
		return;
	}

	g = Cvar_Get("menu_gamma", "1", 0);
	c = Cvar_Get("menu_contrast", "1", 0);
	b = Cvar_Get("menu_brightness", "1", 0);
	m = Cvar_Get("menu_modulate", "1", 0);

	if (g->modified || c->modified || b->modified || m->modified)
	{
		int pw, ph;

		g->modified = c->modified = b->modified = m->modified = false;
		MR_FreePic(d->pic);
		MR_ColorCorrection(g->value, b->value, c->value, m->value * 0.5f);
		MR_GetPicSize(&pw, &ph, d->pic);
		MR_ColorCorrection(Cvar_VariableValue("vid_gamma"), Cvar_VariableValue("vid_brightness"),
				Cvar_VariableValue("vid_contrast"), 1.0f);
	}
}

static const mwclass_t mw_image;

/* MENU-SCRATCH: the pic of an <image> widget (wide layout tabs), else NULL */
const char *
MW_ImagePic(const mwidget_t *w)
{
	return (w && w->cls == &mw_image && w->data) ? ((const image_t *)w->data)->pic : NULL;
}

const char *
MW_ImageLabel(const mwidget_t *w)
{
	return (w && w->cls == &mw_image && w->data && ((const image_t *)w->data)->label[0]) ?
		((const image_t *)w->data)->label : NULL;
}

static void
Image_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	image_t *d = w->data;
	const char *pic = d->pic;
	mcolor_t c = w->tint;

	(void)pg;

	if (d->setimage && Q_stricmp(d->setimage->string, d->pic + strlen("pics/menus/")))
	{
		Q_strlcpy(d->pic, PIC(d->setimage->string), sizeof(d->pic));
		pic = d->pic;
	}

	if (MW_Hovered(w) && !d->setimage)
	{
		c = w->atint;

		if (d->alt[0])
		{
			pic = d->alt;
		}
	}

	if (pic[0])
	{
		MR_StretchPic(x, y, (int)(w->w * w->scale), (int)(w->h * w->scale), c, pic,
				4 + (d->hflip ? 1 : 0) + (d->vflip ? 2 : 0));
	}

	if (d->text[0])
	{
		MR_DrawString(x + d->tx, y + d->ty, d->text, d->texttint, w->font, 0);
	}

	if (d->cvartext)
	{
		MR_DrawString(x + d->cx, y + d->cy, d->cvartext->string, d->texttint, w->font, 0);
	}
}

static qboolean
Image_Key(mpage_t *pg, mwidget_t *w, int key)
{
	(void)pg;
	(void)w;
	(void)key;
	return true;
}

static const mwclass_t mw_image = {
	"pic", Image_Option, NULL, Image_EndPage, NULL, Image_Update, Image_Draw, Image_Key, NULL, NULL
};

static void
Tag_Image(mparse_t *p)
{
	char a[1][MENU_MAX_TOKEN];
	mwidget_t *w;
	image_t *d;
	int pw = -1, ph = -1;

	MP_Args(p, a, 1);

	/* MENU-SCRATCH (owner t95u): <image none ...> holds a slot (a folder
	   tab of the wide layout) without a picture */
	if (!Q_stricmp(a[0], "none"))
	{
		w = ML_NewWidget(p, &mw_image, false);
		d = w->data = Z_Malloc(sizeof(image_t));
		d->texttint = p->page->ftint;
		w->w = 22;
		w->h = w->minh = 22;
		ML_ReadOptions(p, w);
		w->cond = -1;
		ML_AddWidget(p, w);
		return;
	}

	MR_GetPicSize(&pw, &ph, PIC(a[0]));

	if (pw <= 0 || ph <= 0)
	{
		MP_Error(p, "Unable to find \"%s\"", a[0]);
		MP_SkipTag(p);
		return;
	}

	w = ML_NewWidget(p, &mw_image, false);
	d = w->data = Z_Malloc(sizeof(image_t));
	Q_strlcpy(d->pic, PIC(a[0]), sizeof(d->pic));
	d->texttint = p->page->ftint;
	w->w = pw;
	w->h = w->minh = ph;

	/* own pictures (pics/menus/om/...) take any height, also a smaller one */
	if (!Q_strncasecmp(a[0], "om/", 3))
	{
		w->minh = 1;
	}

	ML_ReadOptions(p, w);
	w->minh = w->h;

	/* the picture check happens before options: conditions have no effect */
	w->cond = -1;
	ML_AddWidget(p, w);
}

/* ------------------------------------------------------------------ */
/* MENU-SCRATCH: <omcalib [width W] [height H]> - own picture-adjustment strip
   of the video page, drawn by code: 16 grey steps over a 24-step colour
   wheel (replaces the stock misc/greyscale picture)                     */

static void
Calib_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	int i, h = w->h / 2;

	(void)pg;

	for (i = 0; i < 16; i++)
	{
		int x0 = x + w->w * i / 16, x1 = x + w->w * (i + 1) / 16;
		int v = i * 255 / 15;

		MR_Fill(x0, y, x1 - x0, h, 0xFF000000 | (v << 16) | (v << 8) | v);
	}

	for (i = 0; i < 24; i++)
	{
		int x0 = x + w->w * i / 24, x1 = x + w->w * (i + 1) / 24;
		float hue = i / 4.0f, f = hue - (int)hue;
		int q = (int)(255 * (1 - f)), t = (int)(255 * f), r, g, b;

		switch ((int)hue)
		{
		case 0: r = 255; g = t; b = 0; break;
		case 1: r = q; g = 255; b = 0; break;
		case 2: r = 0; g = 255; b = t; break;
		case 3: r = 0; g = q; b = 255; break;
		case 4: r = t; g = 0; b = 255; break;
		default: r = 255; g = 0; b = q; break;
		}

		MR_Fill(x0, y + h, x1 - x0, w->h - h, 0xFF000000 | (b << 16) | (g << 8) | r);
	}

	MR_FlatBox(x - 1, y - 1, w->w + 2, w->h + 2, 0, 0xFF54473B, 0, false);
}

static const mwclass_t mw_calib = {
	"omcalib", NULL, NULL, NULL, NULL, NULL, Calib_Draw, NULL, NULL, NULL
};

static void
Tag_OmCalib(mparse_t *p)
{
	mwidget_t *w = ML_NewWidget(p, &mw_calib, false);

	w->w = 288;
	w->h = w->minh = 32;
	ML_ReadOptions(p, w);
	w->minh = w->h;
	ML_AddWidget(p, w);
}

/* ------------------------------------------------------------------ */
/* hbar                                                                 */

typedef struct
{
	qboolean pictures, invisible;
	char frame[MAX_QPATH], fill[MAX_QPATH];
	int fw;
} hbar_t;

static qboolean
HBar_Option(mparse_t *p, mwidget_t *w, const char *kw)
{
	hbar_t *d = w->data;

	(void)p;

	if (!Q_stricmp(kw, "invisible"))
	{
		d->invisible = true;
		return true;
	}

	return false;
}

static float
HBar_Value(mwidget_t *w)
{
	float v = w->cvar ? w->cvar->value : 0;
	return v < 0 ? 0 : (v > 1 ? 1 : v);
}

static void
HBar_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	hbar_t *d = w->data;
	float v = HBar_Value(w);

	(void)pg;

	if (d->invisible && v == 0)
	{
		w->flags |= WF_HIDEBORDER;
		return;
	}

	if (!d->pictures)
	{
		if (v != 0)
		{
			MR_Bevel(x, y, (int)(w->w * v), w->h, w->tint);
		}

		return;
	}

	if (d->fw > 0)
	{
		int n = (int)(w->w * v / d->fw), i;

		for (i = 0; i < n; i++)
		{
			MR_DrawPic(x + i * d->fw, y, d->fill, 0xFFFFFFFF);
		}
	}

	MR_DrawPic(x, y, d->frame, 0xFFFFFFFF);
}

static const mwclass_t mw_hbar = {
	"hbar", HBar_Option, NULL, NULL, NULL, NULL, HBar_Draw, NULL, NULL, NULL
};

static void
Tag_HBar(mparse_t *p)
{
	char a[2][MENU_MAX_TOKEN];
	mwidget_t *w;
	hbar_t *d;
	long A, B;

	MP_Args(p, a, 2);
	A = atol(a[0]);
	B = atol(a[1]);

	w = ML_NewWidget(p, &mw_hbar, true);
	d = w->data = Z_Malloc(sizeof(hbar_t));

	if (A == 0 && B == 0)
	{
		int fh;

		d->pictures = true;
		Q_strlcpy(d->frame, PIC(a[0]), sizeof(d->frame));
		Q_strlcpy(d->fill, PIC(a[1]), sizeof(d->fill));
		MR_GetPicSize(&w->w, &w->h, d->frame);
		MR_GetPicSize(&d->fw, &fh, d->fill);
	}
	else
	{
		w->w = (int)A;
		w->h = (int)B;
	}

	w->minh = w->h;
	ML_ReadOptions(p, w);

	if (w->w <= 0 || w->h <= 0 || (d->pictures && d->fw <= 0))
	{
		ML_FreeWidget(w);
		return;
	}

	ML_AddWidget(p, w);
}

/* ------------------------------------------------------------------ */
/* vbar                                                                 */

typedef struct
{
	int ew, eh;
	int L;
	int lastcontent;
} vbar_t;

static void
VBar_Thumb(mpage_t *pg, mwidget_t *w)
{
	vbar_t *d = w->data;
	int H = w->h;

	if (pg->contenth > 0)
	{
		d->L = (H - 6) * (H - 2 * d->eh) / pg->contenth;
	}

	d->lastcontent = pg->contenth;
}

static qboolean
VBar_EndPage(mpage_t *pg, mwidget_t *w)
{
	vbar_t *d = w->data;
	int H = w->h;

	if (H - 2 * d->eh <= 0 || H - 6 <= 0 || pg->contenth <= 0)
	{
		mparse_t dummy;

		memset(&dummy, 0, sizeof(dummy));
		Q_strlcpy(dummy.tag, "vbar", sizeof(dummy.tag));
		MP_Error(&dummy, "Cleanup : Page too small to place vbar");
		pg->vbarw -= 16;
		return false;
	}

	VBar_Thumb(pg, w);
	return true;
}

static void
VBar_Update(mpage_t *pg, mwidget_t *w)
{
	vbar_t *d = w->data;

	if (pg->contenth != d->lastcontent)
	{
		VBar_Thumb(pg, w);
	}
}

static void
VBar_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	vbar_t *d = w->data;
	int H = w->h;
	int ty;

	/* MENU-SCRATCH (owner t95u): own flat scroll bar in the wide layout */
	if (MWD_Enabled())
	{
		MR_FlatBox(x + 2, y, d->ew - 4, d->eh - 1, 0xFF2A231C, 0xFF54473B, 0, false);
		MR_FlatArrow(x + 2, y, d->ew - 4, d->eh - 1, false, 0xFFD8DCD0);
		MR_FlatBox(x + 2, y + H - d->eh + 1, d->ew - 4, d->eh - 1, 0xFF2A231C, 0xFF54473B, 0, false);
		MR_FlatArrow(x + 2, y + H - d->eh + 1, d->ew - 4, d->eh - 1, true, 0xFFD8DCD0);
		MR_FlatBox(x + 5, y + d->eh, d->ew - 10, H - 2 * d->eh, 0xA0000000, 0, 0, false);

		if (pg->contenth > 0)
		{
			ty = y + d->eh + 2 + pg->scroll * (H - 2 * d->eh - 2) / pg->contenth;
			MR_FlatBox(x + 4, ty, d->ew - 8, d->L, 0xFF8C8478, 0, 0, false);
		}

		return;
	}

	MR_StretchPic(x, y, d->ew, d->eh, w->tint, PIC("misc/vend"), 0);
	MR_StretchPic(x, y + H - d->eh, d->ew, d->eh, w->tint, PIC("misc/vend"), 6);
	MR_StretchPic(x, y + d->eh, d->ew, H - 2 * d->eh, w->tint, PIC("misc/vbar"), 0);

	if (pg->contenth > 0)
	{
		ty = y + d->eh + 2 + pg->scroll * (H - 2 * d->eh - 2) / pg->contenth;
		MR_Bevel(x + 3, ty, 10, d->L, w->tint);
	}
}

static void
VBar_Activate(mpage_t *pg, mwidget_t *w, int button, int mx, int my)
{
	vbar_t *d = w->data;
	int H = w->h;

	(void)mx;

	if (button != 1)
	{
		return;
	}

	if (my < d->eh)
	{
		pg->scroll -= 4;
	}
	else if (my > H - d->eh)
	{
		pg->scroll += 4;
	}
	else if (H - 2 * d->eh > 0 && H > 0)
	{
		int m = my > d->eh ? my : d->eh;
		pg->scroll = ((m - d->eh) * H / (H - 2 * d->eh) - d->L / 2) * pg->contenth / H;
		pg->drag = w;
	}

	ML_ClampScroll(pg);
}

static const mwclass_t mw_vbar = {
	"vbar", NULL, NULL, VBar_EndPage, NULL, VBar_Update, VBar_Draw, NULL, VBar_Activate, NULL
};

static void
Tag_VBar(mparse_t *p)
{
	mpage_t *pg = p->page;
	mwidget_t *w = ML_NewWidget(p, &mw_vbar, false);
	vbar_t *d = w->data = Z_Malloc(sizeof(vbar_t));

	if (MWD_Enabled())
	{
		d->ew = d->eh = 16; /* own flat scroll bar (VBar_Draw) */
	}
	else
	{
		MR_GetPicSize(&d->ew, &d->eh, PIC("misc/vend"));
	}

	if (d->ew < 0)
	{
		d->ew = d->eh = 0;
	}

	w->w = 16;
	w->h = w->minh = ML_ContentH(pg);
	w->x = ML_ContentW(pg) - 16;
	w->y = 0;
	w->flags |= WF_NOTPLACED | WF_NOSCROLL;
	ML_ReadOptions(p, w);
	pg->vbarw += 16;

	if (!pg->vbar)
	{
		pg->vbar = w;
	}

	ML_AddWidget(p, w);
}

/* ------------------------------------------------------------------ */
/* ticker                                                               */

typedef struct
{
	int speed;
	float delay;
} ticker_t;

static qboolean
Ticker_Option(mparse_t *p, mwidget_t *w, const char *kw)
{
	ticker_t *d = w->data;
	char a[1][MENU_MAX_TOKEN];

	if (!Q_stricmp(kw, "speed"))
	{
		MP_Args(p, a, 1);
		d->speed = atoi(a[0]);
		return true;
	}

	if (!Q_stricmp(kw, "delay"))
	{
		MP_Args(p, a, 1);
		d->delay = (float)atof(a[0]);
		return true;
	}

	return false;
}

static void
Ticker_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	ticker_t *d = w->data;
	mscreen_t *scr = pg->screen;
	double t = (ms.now - scr->opentime) / 1000.0;
	int p = (int)floor((t - d->delay) * d->speed * 10.0);
	int sw = MW_SW(w->text, w->font);
	int span = sw + w->w, offset;

	if (!scr->ready)
	{
		return;
	}

	/* MENU-SCRATCH: a wide layout ticker that fits stands still */
	if (w->omslot && sw <= w->w)
	{
		MR_SetClip(x, y, w->w, w->h);
		MR_DrawString(x, y, w->text, w->tint, w->font, 0);
		MR_SetClip(pg->clip[0], pg->clip[1], pg->clip[2], pg->clip[3]);
		return;
	}

	offset = (span > 0 ? (p % span) : 0) - w->w;

	/* MENU-SCRATCH: a wide layout ticker that does not fit starts readable at
	   the left edge, then scrolls and comes back in from the right */
	if (w->omslot)
	{
		offset = span > 0 ? (p > 0 ? p : 0) % span : 0;

		if (offset > sw)
		{
			offset -= span;
		}
	}

	{
		/* widget rectangle, intersected with the frame content area unless global */
		int x0 = x, y0 = y, x1 = x + w->w, y1 = y + w->h;

		if (!pg->global)
		{
			mframe_t *f = pg->frame;
			int cx0 = f->fx + f->pad + scr->originx, cy0 = f->fy + f->pad + scr->originy;
			int cx1 = cx0 + ML_ContentW(pg), cy1 = f->fy + f->fh - f->pad + scr->originy;

			x0 = x0 > cx0 ? x0 : cx0;
			y0 = y0 > cy0 ? y0 : cy0;
			x1 = x1 < cx1 ? x1 : cx1;
			y1 = y1 < cy1 ? y1 : cy1;
		}

		if (x1 <= x0 || y1 <= y0)
		{
			return;
		}

		MR_SetClip(x0, y0, x1 - x0, y1 - y0);
		MR_DrawString(x - offset, y, w->text, w->tint, w->font, 0);
		MR_SetClip(pg->clip[0], pg->clip[1], pg->clip[2], pg->clip[3]);
	}
}

static const mwclass_t mw_ticker = {
	"ticker", Ticker_Option, NULL, NULL, NULL, NULL, Ticker_Draw, NULL, NULL, NULL
};

static void
Tag_Ticker(mparse_t *p)
{
	char a[1][MENU_MAX_TOKEN];
	mwidget_t *w;
	ticker_t *d;

	MP_Args(p, a, 1);
	w = ML_NewWidget(p, &mw_ticker, true);
	d = w->data = Z_Malloc(sizeof(ticker_t));
	d->speed = 1;
	d->delay = 1.0f;
	w->text = CopyString(a[0]);
	w->w = ML_ContentW(p->page);
	w->h = w->minh = MW_FH(w);
	ML_ReadOptions(p, w);
	ML_AddWidget(p, w);
}

/* ------------------------------------------------------------------ */
/* hr                                                                   */

typedef struct
{
	int length, line;
} hr_t;

static void
HR_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	hr_t *d = w->data;
	int tw = ML_TotalW(w), th = ML_TotalH(w);
	int x0 = x + (tw - d->length) / 2;

	(void)pg;
	MR_Line((float)x0, (float)(y + th / 2), (float)(x0 + d->length), (float)(y + th / 2), (float)d->line, w->tint);
}

static const mwclass_t mw_hr = {
	"hr", NULL, NULL, NULL, NULL, NULL, HR_Draw, NULL, NULL, NULL
};

static void
Tag_HR(mparse_t *p)
{
	char kw[MENU_MAX_TOKEN], a[1][MENU_MAX_TOKEN];
	mpage_t *pg = p->page;
	mwidget_t *w;
	hr_t *d;
	int t;

	ML_BreakLine(pg, true);

	w = ML_NewWidget(p, &mw_hr, false);
	d = w->data = Z_Malloc(sizeof(hr_t));
	w->flags &= ~WF_HASBORDER;
	w->bpad = w->bthick = 0;
	w->w = ML_ContentW(pg);
	w->h = 8;
	d->length = w->w;
	d->line = 1;
	w->align = MA_NORMAL;

	/* own options only; other words are ignored silently */
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

		if (!Q_stricmp(kw, "width"))
		{
			MP_Args(p, a, 1);
			d->length = atoi(a[0]);
		}
		else if (!Q_stricmp(kw, "line"))
		{
			MP_Args(p, a, 1);
			d->line = atoi(a[0]);
		}
		else if (!Q_stricmp(kw, "tint"))
		{
			MP_Args(p, a, 1);
			w->tint = w->atint = MP_Color(p->screen, a[0]);
		}
		else if (!Q_stricmp(kw, "height"))
		{
			int h;

			MP_Args(p, a, 1);
			h = atoi(a[0]);

			if (h > w->h)
			{
				w->h = h;
			}
		}
	}

	pg->penx = 0;
	ML_AddWidget(p, w);
	ML_BreakLine(pg, true);
}

/* ------------------------------------------------------------------ */
/* blank                                                                */

static qboolean
Blank_Key(mpage_t *pg, mwidget_t *w, int key)
{
	(void)pg;
	(void)w;
	(void)key;
	return true;
}

static const mwclass_t mw_blank = {
	"blank", NULL, NULL, NULL, NULL, NULL, NULL, Blank_Key, NULL, NULL
};

static void
Tag_Blank(mparse_t *p)
{
	char a[2][MENU_MAX_TOKEN];
	mwidget_t *w;

	MP_Args(p, a, 2);
	w = ML_NewWidget(p, &mw_blank, false);
	w->w = atoi(a[0]);
	w->h = w->minh = atoi(a[1]);
	ML_ReadOptions(p, w);
	ML_AddWidget(p, w);
}

/* ------------------------------------------------------------------ */
/* selection (outfitting)                                               */

#define SEL_MAX 32

typedef struct
{
	char cellpic[MAX_QPATH];
	int cw, ch, columns;
	int category;      /* 0 weapons, 1 items, 2 ammo, 3 misc */
	int count;
	char cvars[SEL_MAX][64];
	char pics[SEL_MAX][MAX_QPATH];
	char ammo[SEL_MAX][64];
	int cells[SEL_MAX];
} selection_t;

static qboolean
Selection_Option(mparse_t *p, mwidget_t *w, const char *kw)
{
	selection_t *d = w->data;
	static const char *cats[] = {"weapons", "items", "ammo", "misc"};
	int i;

	(void)p;

	for (i = 0; i < 4; i++)
	{
		if (!Q_stricmp(kw, cats[i]))
		{
			d->category = i;
			return true;
		}
	}

	return false;
}

static qboolean
Selection_Finish(mparse_t *p, mwidget_t *w)
{
	selection_t *d = w->data;
	mscreen_t *scr = p->screen;

	if (scr->selections[d->category] && !p->dry)
	{
		Com_Printf("Error : Only one selection allowed per type per page\n");
		return false;
	}

	if (!p->dry)
	{
		scr->selections[d->category] = w;
	}

	return true;
}

static int
Selection_Used(const selection_t *d)
{
	int i, n = 0;

	for (i = 0; i < d->count; i++)
	{
		n += d->cells[i];
	}

	return n;
}

static void
Selection_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	selection_t *d = w->data;
	int i, cx = x, used = 0;

	(void)pg;

	for (i = 0; i < d->count; i++)
	{
		int pw, ph, span = d->cells[i] * d->cw;
		float v;

		MR_GetPicSize(&pw, &ph, d->pics[i]);

		if (pw > 0)
		{
			MR_DrawPic(cx + (span - pw) / 2, y, d->pics[i], w->tint);
		}

		v = Cvar_VariableValue(d->cvars[i]);

		if (v > 1)
		{
			MR_DrawString(cx, y, va("%d", (int)v), w->dtint, "title", 0);
		}

		if (d->ammo[i][0] && Q_stricmp(d->ammo[i], "none") && Cvar_VariableValue(d->ammo[i]) != 0)
		{
			MR_DrawString(cx, y + 40, va("%d", (int)Cvar_VariableValue(d->ammo[i])), w->ctint, "title", 0);
		}

		cx += span;
		used += d->cells[i];
	}

	for (; used < d->columns; used++, cx += d->cw)
	{
		MR_DrawPic(cx, y, d->cellpic, w->tint);
	}
}

static qboolean
Selection_Key(mpage_t *pg, mwidget_t *w, int key)
{
	selection_t *d = w->data;
	int x, y, i, cell, at = 0;
	qboolean removed = false;

	if (key != K_MOUSE1)
	{
		return true;
	}

	float fx, fy;

	ML_WidgetScreenPos(pg, w, &x, &y);
	ML_PageMouse(pg, w, &fx, &fy);
	cell = d->cw > 0 ? ((int)fx - x) / d->cw : -1;

	for (i = 0; i < d->count && cell >= 0; i++)
	{
		if (cell >= at && cell < at + d->cells[i])
		{
			int n = d->count - i - 1;

			Cvar_Set(d->cvars[i], "0");
			memmove(&d->cvars[i], &d->cvars[i + 1], n * sizeof(d->cvars[0]));
			memmove(&d->pics[i], &d->pics[i + 1], n * sizeof(d->pics[0]));
			memmove(&d->ammo[i], &d->ammo[i + 1], n * sizeof(d->ammo[0]));
			memmove(&d->cells[i], &d->cells[i + 1], n * sizeof(d->cells[0]));
			d->count--;
			removed = true;
			break;
		}

		at += d->cells[i];
	}

	if (!removed)
	{
		M_PlaySound("menu/small/invalid.wav");
	}

	M_PlaySound("menu/small/select.wav");
	return false; /* MOUSE1 does not run the key list */
}

static const mwclass_t mw_selection = {
	"selection", Selection_Option, Selection_Finish, NULL, NULL, NULL, Selection_Draw, Selection_Key, NULL, NULL
};

static void
Tag_Selection(mparse_t *p)
{
	char a[3][MENU_MAX_TOKEN];
	mwidget_t *w;
	selection_t *d;

	MP_Args(p, a, 3);
	w = ML_NewWidget(p, &mw_selection, false);
	d = w->data = Z_Malloc(sizeof(selection_t));
	Q_strlcpy(d->cellpic, PIC(a[0]), sizeof(d->cellpic));
	MR_GetPicSize(&d->cw, &d->ch, d->cellpic);
	w->w = atoi(a[1]);
	w->h = w->minh = atoi(a[2]);
	d->columns = d->cw > 0 ? w->w / d->cw : 0;
	w->flags |= WF_INTERACTIVE;
	ML_ReadOptions(p, w);
	ML_AddWidget(p, w);
}

/* console command "select <cvar>" (SPEC-801 section 4) */
void
MW_SelectCommand(int argc)
{
	mscreen_t *scr = M_TopScreen();
	cvar_t *v;
	int cat;
	mwidget_t *w;
	selection_t *d;
	char info[256], *f[4], *s;
	int n = 0, cells, pw, ph;

	if (argc < 2 || !scr)
	{
		return;
	}

	v = Cvar_Find(Cmd_Argv(1));

	if (!v)
	{
		return;
	}

	cat = (v->flags & CVAR_WEAPON) ? 0 : (v->flags & CVAR_ITEM) ? 1 : (v->flags & CVAR_AMMO) ? 2 : 3;
	w = scr->selections[cat];

	if (!w)
	{
		return;
	}

	d = w->data;

	if (v->value != 0)
	{
		M_PlaySound("menu/small/invalid.wav");
		return;
	}

	Q_strlcpy(info, Cvar_VariableString(va("%s_info", v->name)), sizeof(info));

	for (s = info, f[n++] = s; *s && n < 4; s++)
	{
		if (*s == ',')
		{
			*s = 0;
			f[n++] = s + 1;
		}
	}

	while (n < 4)
	{
		f[n++] = "";
	}

	MR_GetPicSize(&pw, &ph, PIC(f[0]));

	if (pw <= 0)
	{
		return;
	}

	cells = atoi(f[3]);

	if (cells < 1)
	{
		cells = 1;
	}

	if (Selection_Used(d) + cells > d->columns || d->count >= SEL_MAX)
	{
		M_PlaySound("menu/small/invalid.wav");
		return;
	}

	Cvar_Set(v->name, f[1]);

	if (Q_stricmp(f[2], "none") && f[2][0] && Cvar_VariableValue(f[2]) == 0)
	{
		char ainfo[256], *c;

		Q_strlcpy(ainfo, Cvar_VariableString(va("%s_info", f[2])), sizeof(ainfo));
		c = strchr(ainfo, ',');

		if (c)
		{
			char *e;

			c++;
			e = strchr(c, ',');

			if (e)
			{
				*e = 0;
			}

			Cvar_Set(f[2], c);
		}
	}

	Q_strlcpy(d->cvars[d->count], v->name, sizeof(d->cvars[0]));
	Q_strlcpy(d->pics[d->count], PIC(f[0]), sizeof(d->pics[0]));
	Q_strlcpy(d->ammo[d->count], f[2], sizeof(d->ammo[0]));
	d->cells[d->count] = cells;
	d->count++;
}

/* ------------------------------------------------------------------ */
/* popup: accepted, never drawn (unfinished in 1.07f)                   */

static const mwclass_t mw_popup = {
	"popup", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static void
Tag_Popup(mparse_t *p)
{
	char a[1][MENU_MAX_TOKEN];
	mwidget_t *w;
	char **items;
	int count, i, widest = 0;

	MP_Args(p, a, 1);

	if (p->page->popups)
	{
		MP_Error(p, "Only 1 popup allowed per page");
		MP_SkipTag(p);
		return;
	}

	p->page->popups++;
	w = ML_NewWidget(p, &mw_popup, true);
	MW_SplitComma(a[0], &items, &count);

	for (i = 0; i < count; i++)
	{
		int sw = MW_SW(items[i], w->font);
		widest = sw > widest ? sw : widest;
	}

	MW_FreeList(items, count);
	w->w = widest + 8;
	w->h = w->minh = (MW_FH(w) + 4) * count + 8;
	ML_ReadOptions(p, w);
	w->flags |= WF_NOTPLACED;
	ML_AddWidget(p, w);
}

/* ------------------------------------------------------------------ */

qboolean
MW_Tag(mparse_t *p, const char *tag)
{
	static const struct
	{
		const char *name;
		void (*fn)(mparse_t *p);
	} tags[] = {
		{"text", Tag_Text},
		{"ctext", Tag_CText},
		{"list", Tag_List},
		{"slider", Tag_Slider},
		{"input", Tag_Input},
		{"setkey", Tag_SetKey},
		{"image", Tag_Image},
		{"omcalib", Tag_OmCalib},
		{"hbar", Tag_HBar},
		{"vbar", Tag_VBar},
		{"ticker", Tag_Ticker},
		{"hr", Tag_HR},
		{"blank", Tag_Blank},
		{"selection", Tag_Selection},
		{"popup", Tag_Popup},
		{NULL, NULL}
	};
	int i;

	for (i = 0; tags[i].name; i++)
	{
		if (!Q_stricmp(tag, tags[i].name))
		{
			tags[i].fn(p);
			return true;
		}
	}

	return false;
}
