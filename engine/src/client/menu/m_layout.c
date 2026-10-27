/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc: menu widgets common part - creation, common item options, key
 * bindings, flow layout, and drawing of frames, pages and borders.
 *
 * Implements: SPEC-800 (sections 5.4, 6, 7, 8 drawing, 9, 10)
 */

#include "header/menu.h"
#include "header/m_draw.h"

/* ------------------------------------------------------------------ */
/* key bindings                                                        */

mbind_t *
ML_NewBind(int key, const char *cmd0, const char *cmd1, const char *cvar, qboolean ckey)
{
	mbind_t *b = Z_Malloc(sizeof(*b));

	b->key = key;
	b->cmd0 = CopyString(cmd0 ? cmd0 : "");
	b->cmd1 = cmd1 ? CopyString(cmd1) : NULL;
	b->ckey = ckey;

	if (cvar)
	{
		Q_strlcpy(b->cvar, cvar, sizeof(b->cvar));
	}

	return b;
}

void
ML_AddIKey(mbind_t **list, const char *bound, const char *cmd)
{
	int k;

	/* one binding for every key currently bound to the command */
	for (k = 0; k < K_LAST; k++)
	{
		if (keybindings[k] && keybindings[k][0] && !Q_stricmp(keybindings[k], bound))
		{
			mbind_t *b = ML_NewBind(k, cmd, NULL, NULL, false);
			b->next = *list;
			*list = b;
		}
	}
}

void
ML_FreeBinds(mbind_t *b)
{
	while (b)
	{
		mbind_t *n = b->next;

		Z_Free(b->cmd0);

		if (b->cmd1)
		{
			Z_Free(b->cmd1);
		}

		Z_Free(b);
		b = n;
	}
}

/* run the bindings matching the key; widget bindings play select.wav */
qboolean
ML_RunKeyList(mpage_t *pg, mbind_t *b, int key, qboolean sound)
{
	qboolean any = false;

	(void)pg;

	/* the list is kept newest first; run in declaration order */
	{
		mbind_t *order[64];
		int n = 0, i;

		for (; b && n < 64; b = b->next)
		{
			order[n++] = b;
		}

		for (i = n - 1; i >= 0; i--)
		{
			mbind_t *m = order[i];
			const char *cmd;

			if (m->key != -1 && m->key != key)
			{
				continue;
			}

			cmd = m->cmd0;

			if (m->ckey && Cvar_VariableValue(m->cvar) != 0)
			{
				cmd = m->cmd1 ? m->cmd1 : "";
			}

			if (sound)
			{
				M_PlaySound("menu/small/select.wav");
			}

			M_QueueCommand(cmd, false);
			any = true;
		}
	}

	return any;
}

/* ------------------------------------------------------------------ */
/* sizes                                                               */

int
ML_TotalW(const mwidget_t *w)
{
	return (int)(w->w * w->scale) + 2 * w->bpad;
}

int
ML_TotalH(const mwidget_t *w)
{
	return (int)(w->h * w->scale) + 2 * w->bpad;
}

/* available width A of SPEC-800 section 6 */
int
ML_ContentW(const mpage_t *pg)
{
	if (!pg->frame)
	{
		return 0;
	}

	return pg->frame->fw - 2 * pg->frame->pad - pg->vbarw;
}

int
ML_ContentH(const mpage_t *pg)
{
	if (!pg->frame)
	{
		return 0;
	}

	return pg->frame->fh - 2 * pg->frame->pad;
}

int
ML_FontHeight(const char *font)
{
	return MR_FontHeight(font);
}

/* ------------------------------------------------------------------ */
/* widget creation and common options (SPEC-800 section 7)              */

mwidget_t *
ML_NewWidget(mparse_t *p, const mwclass_t *cls, qboolean pagefont)
{
	mpage_t *pg = p->page;
	mwidget_t *w = Z_Malloc(sizeof(*w));

	w->cls = cls;
	w->page = pg;
	w->scale = 1.0f;
	w->cond = -1;
	w->tint = w->atint = 0xFFFFFFFF;
	w->btint = 0x00000000;
	w->ctint = w->dtint = 0xFFFFFFFF;
	Q_strlcpy(w->font, pg->font, sizeof(w->font));

	if (pagefont)
	{
		w->tint = pg->ftint;
		w->atint = pg->fatint;
	}

	w->align = pg->align;

	if (pg->dborder)
	{
		w->bpad = pg->dbpad;
		w->bthick = pg->dbthick;
		w->bcolor = pg->dbcolor;
		w->flags |= WF_HASBORDER;
	}

	return w;
}

void
ML_FreeWidget(mwidget_t *w)
{
	if (!w)
	{
		return;
	}

	if (w->cls && w->cls->freedata)
	{
		w->cls->freedata(w);
	}
	else if (w->data)
	{
		Z_Free(w->data);
	}

	ML_FreeBinds(w->binds);

	if (w->tip)
	{
		Z_Free(w->tip);
	}

	if (w->text)
	{
		Z_Free(w->text);
	}

	Z_Free(w);
}

static int
ML_KeyNum(const char *name)
{
	if (!Q_stricmp(name, "any"))
	{
		return -1;
	}

	return Key_StringToKeynum(name);
}

static void
ML_AddBind(mwidget_t *w, mbind_t *b)
{
	b->next = w->binds;
	w->binds = b;
}

static cvar_t *
ML_GetCvar(mparse_t *p, const char *name, const char *def, int flags)
{
	cvar_t *v = Cvar_GetModule(name, def, flags, NULL);

	if (!v && !p->dry)
	{
		/* hidden cvar while hidden cvars are locked: the page fails */
		p->page->failed = true;
	}

	return v;
}

qboolean
ML_CommonOption(mparse_t *p, mwidget_t *w, const char *kw)
{
	char a[4][MENU_MAX_TOKEN];
	mscreen_t *scr = p->screen;

	if (!Q_stricmp(kw, "key"))
	{
		MP_Args(p, a, 2);
		ML_AddBind(w, ML_NewBind(ML_KeyNum(a[0]), a[1], NULL, NULL, false));
		w->flags |= WF_INTERACTIVE;
	}
	else if (!Q_stricmp(kw, "ckey"))
	{
		MP_Args(p, a, 4);

		if (!p->dry)
		{
			Cvar_Get(a[1], "0", 0);
		}

		ML_AddBind(w, ML_NewBind(ML_KeyNum(a[0]), a[2], a[3], a[1], true));
		w->flags |= WF_INTERACTIVE;
	}
	else if (!Q_stricmp(kw, "ikey"))
	{
		MP_Args(p, a, 2);
		ML_AddIKey(&w->binds, a[0], a[1]);
		w->flags |= WF_INTERACTIVE;
	}
	else if (!Q_stricmp(kw, "next") || !Q_stricmp(kw, "prev"))
	{
		ML_AddBind(w, ML_NewBind(K_MOUSE1, !Q_stricmp(kw, "next") ? "menu $menu_next" : "menu $menu_prev",
				NULL, NULL, false));
		w->flags |= WF_INTERACTIVE;
	}
	else if (!Q_stricmp(kw, "tip"))
	{
		MP_Args(p, a, 1);

		if (w->tip)
		{
			Z_Free(w->tip);
		}

		w->tip = CopyString(a[0]);
		w->flags |= WF_INTERACTIVE;
	}
	else if (!Q_stricmp(kw, "inc"))
	{
		MP_Args(p, a, 1);
		w->inc = (float)atof(a[0]);
		w->hasinc = true;
		w->flags |= WF_INTERACTIVE;
	}
	else if (!Q_stricmp(kw, "mod"))
	{
		MP_Args(p, a, 1);
		w->mod = (float)atof(a[0]);
		w->hasmod = true;
	}
	else if (!Q_stricmp(kw, "cvar"))
	{
		MP_Args(p, a, 1);
		w->cvar = ML_GetCvar(p, a[0], "", 0);
	}
	else if (!Q_stricmp(kw, "cvari"))
	{
		MP_Args(p, a, 1);
		w->cvar = ML_GetCvar(p, a[0], "0", CVAR_INTEGER);
	}
	else if (!Q_stricmp(kw, "tint"))
	{
		MP_Args(p, a, 1);
		w->tint = MP_Color(scr, a[0]);
	}
	else if (!Q_stricmp(kw, "atint"))
	{
		MP_Args(p, a, 1);
		w->atint = MP_Color(scr, a[0]);
	}
	else if (!Q_stricmp(kw, "btint"))
	{
		MP_Args(p, a, 1);
		w->btint = MP_Color(scr, a[0]);
	}
	else if (!Q_stricmp(kw, "ctint"))
	{
		MP_Args(p, a, 1);
		w->ctint = MP_Color(scr, a[0]);
	}
	else if (!Q_stricmp(kw, "dtint"))
	{
		MP_Args(p, a, 1);
		w->dtint = MP_Color(scr, a[0]);
	}
	else if (!Q_stricmp(kw, "noshade"))
	{
		w->flags |= WF_NOSHADE;
	}
	else if (!Q_stricmp(kw, "noscale"))
	{
		w->flags |= WF_NOSCALE;
	}
	else if (!Q_stricmp(kw, "border"))
	{
		MP_Args(p, a, 3);
		w->bpad = atoi(a[0]);
		w->bthick = atoi(a[1]);
		w->bcolor = MP_Color(scr, a[2]);
		w->flags |= WF_HASBORDER;
	}
	else if (!Q_stricmp(kw, "noborder"))
	{
		w->bpad = w->bthick = 0;
		w->bcolor = 0;
		w->flags &= ~WF_HASBORDER;
	}
	else if (!Q_stricmp(kw, "width"))
	{
		MP_Args(p, a, 1);
		w->w = atoi(a[0]);
	}
	else if (!Q_stricmp(kw, "height"))
	{
		int h;

		MP_Args(p, a, 1);
		h = atoi(a[0]);

		if (h > w->minh)
		{
			w->h = h;
			w->minh = h;
		}
	}
	else if (!Q_stricmp(kw, "xoff"))
	{
		MP_Args(p, a, 1);
		w->xoff = (int)(atof(a[0]) * w->scale);
	}
	else if (!Q_stricmp(kw, "yoff"))
	{
		MP_Args(p, a, 1);
		w->yoff = (int)(atof(a[0]) * w->scale);
	}
	else if (!Q_stricmp(kw, "nudge"))
	{
		/* MENU-SCRATCH: extra offset kept after the <center>/<right>
		   bolt alignment (which overwrites xoff/yoff) */
		MP_Args(p, a, 2);
		w->nudgex = (int)(atof(a[0]) * w->scale);
		w->nudgey = (int)(atof(a[1]) * w->scale);
	}
	else if (!Q_stricmp(kw, "split"))
	{
		/* MENU-SCRATCH: two columns around the row centre: the label
		   right-aligned left of it, the value left-aligned right of it */
		MP_Args(p, a, 1);
		w->split = (int)(atof(a[0]) * w->scale);
	}
	else if (!Q_stricmp(kw, "tab"))
	{
		w->flags |= WF_TAB;
	}
	else if (!Q_stricmp(kw, "align"))
	{
		MP_Args(p, a, 1);
		w->flags |= WF_SAWALIGN;

		if (!Q_stricmp(a[0], "left"))
		{
			w->align = MA_LEFT;
		}
		else if (!Q_stricmp(a[0], "right"))
		{
			w->align = MA_RIGHT;
		}
		else if (!Q_stricmp(a[0], "center"))
		{
			w->align = MA_CENTER;
		}
		else
		{
			MP_Error(p, "Invalid align parameter");
			w->align = MA_LEFT;
		}
	}
	else if (!Q_stricmp(kw, "bolt") || !Q_stricmp(kw, "bbolt"))
	{
		MP_Args(p, a, 1);
		MM_BoltOption(p, w, a[0], !Q_stricmp(kw, "bbolt"));
	}
	else if (!Q_stricmp(kw, "ifeq") || !Q_stricmp(kw, "ifne") || !Q_stricmp(kw, "iflt") ||
			!Q_stricmp(kw, "ifgt") || !Q_stricmp(kw, "ifle") || !Q_stricmp(kw, "ifge"))
	{
		float v, c;

		MP_Args(p, a, 2);
		v = Cvar_VariableValue(a[0]);
		c = (float)atof(a[1]);

		switch (kw[3] | 0x20)
		{
			case 'q': w->cond = (v == c); break;
			case 't': w->cond = ((kw[2] | 0x20) == 'l') ? (v < c) : (v > c); break;
			case 'e': w->cond = ((kw[2] | 0x20) == 'l') ? (v <= c) : (v >= c); break;
			default: w->cond = (v != c); break; /* ifne */
		}
	}
	else if (!Q_stricmp(kw, "ifset") || !Q_stricmp(kw, "ifclr"))
	{
		int v, bit;

		MP_Args(p, a, 2);
		v = (int)Cvar_VariableValue(a[0]);
		bit = atoi(a[1]);
		w->cond = ((v >> bit) & 1) ? 1 : 0;

		if (!Q_stricmp(kw, "ifclr"))
		{
			w->cond = !w->cond;
		}
	}
	else
	{
		return false;
	}

	return true;
}

/* options up to '>'; an unknown option ends the tag without consuming
   the rest (SPEC-800 section 4) */
void
ML_ReadOptions(mparse_t *p, mwidget_t *w)
{
	char kw[MENU_MAX_TOKEN];
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

		if (w->cls->option && w->cls->option(p, w, kw))
		{
			continue;
		}

		if (ML_CommonOption(p, w, kw))
		{
			continue;
		}

		MP_Error(p, "Unrecognised %s keyword", w->cls->name);
		return;
	}
}

/* ------------------------------------------------------------------ */
/* flow layout (SPEC-800 section 6)                                     */

void
ML_BreakLine(mpage_t *pg, qboolean hbr)
{
	pg->penx = 0;

	if (pg->linebottom == pg->linetop)
	{
		pg->linebottom += ML_FontHeight(pg->font);
	}

	pg->linetop = pg->peny = pg->linebottom;

	if (pg->linebottom > pg->contenth)
	{
		pg->contenth = pg->linebottom;
	}

	if (hbr)
	{
		pg->anchor = pg->linetop;
	}
}

void
ML_SetAlign(mpage_t *pg, int align)
{
	pg->align = align;
	pg->linetop = pg->linebottom = pg->anchor;
}

static qboolean
ML_Placed(const mwidget_t *o)
{
	return !(o->flags & (WF_BOLTED | WF_NOTPLACED));
}

/* first placed widget overlapping the rectangle (strict inequalities) */
static mwidget_t *
ML_Overlap(mpage_t *pg, int x, int y, int w, int h)
{
	int i;

	for (i = 0; i < pg->numwidgets; i++)
	{
		mwidget_t *o = pg->widgets[i];
		int ow, oh;

		if (!ML_Placed(o))
		{
			continue;
		}

		ow = ML_TotalW(o);
		oh = ML_TotalH(o);

		if (x < o->x + ow && o->x < x + w && y < o->y + oh && o->y < y + h)
		{
			return o;
		}
	}

	return NULL;
}

static void
ML_Place(mpage_t *pg, mwidget_t *w)
{
	int A = ML_ContentW(pg);
	int tw = ML_TotalW(w), th = ML_TotalH(w);
	int fh = ML_FontHeight(pg->font);
	int x, y, guard = 0;
	mwidget_t *o;

	y = (w->flags & WF_TEXTLIKE) ? pg->peny : pg->linetop;

	switch (w->align)
	{
		case MA_CENTER:
			x = (A - tw) / 2;

			while ((o = ML_Overlap(pg, x, y, tw, th)) != NULL && guard++ < 10000)
			{
				y = o->y + ML_TotalH(o);
			}

			break;

		case MA_LEFT:
		case MA_RIGHT:
			for (;;)
			{
				int bottom = y;
				qboolean restart = false;

				x = (w->align == MA_LEFT) ? 0 : A - tw;

				while ((o = ML_Overlap(pg, x, y, tw, th)) != NULL && guard++ < 10000)
				{
					if (o->y + ML_TotalH(o) > bottom)
					{
						bottom = o->y + ML_TotalH(o);
					}

					x = (w->align == MA_LEFT) ? o->x + ML_TotalW(o) : o->x - tw;

					if ((w->align == MA_LEFT && x + tw > A) || (w->align == MA_RIGHT && x < 0))
					{
						restart = true;
						break;
					}
				}

				if (!restart || guard >= 10000)
				{
					break;
				}

				y = bottom;
			}

			break;

		default:
			x = pg->penx;

			for (;;)
			{
				if (x + tw > A)
				{
					x = 0;
					y += fh;
				}

				o = ML_Overlap(pg, x, y, tw, th);

				if (!o || guard++ >= 10000)
				{
					break;
				}

				x = o->x + ML_TotalW(o);
			}

			break;
	}

	w->x = x;
	w->y = y;
	pg->penx = x + tw;
	pg->peny = y;

	if (y + th > pg->linebottom)
	{
		pg->linebottom = y + th;
	}

	if (pg->linebottom > pg->contenth)
	{
		pg->contenth = pg->linebottom;
	}
}

static void
ML_Append(mpage_t *pg, mwidget_t *w)
{
	if (pg->numwidgets == pg->maxwidgets)
	{
		int n = pg->maxwidgets ? pg->maxwidgets * 2 : 32;
		mwidget_t **nw = Z_Malloc(n * sizeof(*nw));

		if (pg->widgets)
		{
			memcpy(nw, pg->widgets, pg->numwidgets * sizeof(*nw));
			Z_Free(pg->widgets);
		}

		pg->widgets = nw;
		pg->maxwidgets = n;
	}

	pg->widgets[pg->numwidgets++] = w;
}

/* finish a widget tag: conditions, placement, append (SPEC-800 6/7) */
void
ML_AddWidget(mparse_t *p, mwidget_t *w)
{
	mpage_t *pg = p->page;

	if (pg->variable)
	{
		MP_Error(p, "No rects allowed after a variable one");
		ML_FreeWidget(w);
		return;
	}

	if (w->cond == 0)
	{
		ML_FreeWidget(w);
		return;
	}

	if (w->cls->finish && !w->cls->finish(p, w))
	{
		ML_FreeWidget(w);
		return;
	}

	if (w->flags & WF_BOLTED)
	{
		int tw = ML_TotalW(w), th = ML_TotalH(w);

		if (w->align == MA_CENTER)
		{
			w->xoff = (int)((-tw >> 1) * w->scale);
			w->yoff = (int)((-th >> 1) * w->scale);
		}
		else if (w->align == MA_RIGHT)
		{
			w->xoff = (int)(-tw * w->scale);
			w->yoff = (int)(-th * w->scale);
		}

		w->xoff += w->nudgex;
		w->yoff += w->nudgey;
	}
	else if (!(w->flags & WF_NOTPLACED))
	{
		if (ML_TotalW(w) > ML_ContentW(pg))
		{
			MP_Error(p, "Object too wide to place");
			ML_FreeWidget(w);
			return;
		}

		ML_Place(pg, w);
	}

	ML_Append(pg, w);

	if (w->flags & WF_VARIABLE)
	{
		pg->variable = true;
	}
}

/* centre / right re-positioning after a size change (ctext, list) */
void
ML_Reposition(mpage_t *pg, mwidget_t *w)
{
	int tw = ML_TotalW(w);

	if (w->flags & WF_BOLTED)
	{
		if (w->align == MA_CENTER)
		{
			w->xoff = -tw / 2;
		}
		else if (w->align == MA_RIGHT)
		{
			w->xoff = -tw;
		}

		return;
	}

	if (w->align == MA_CENTER)
	{
		w->x = (ML_ContentW(pg) - tw) / 2;
	}
	else if (w->align == MA_RIGHT)
	{
		w->x = ML_ContentW(pg) - tw;
	}
}

void
ML_ClampScroll(mpage_t *pg)
{
	int max = pg->contenth - ML_ContentH(pg);

	if (pg->scroll > max)
	{
		pg->scroll = max;
	}

	if (pg->scroll < 0)
	{
		pg->scroll = 0;
	}
}

/* ------------------------------------------------------------------ */
/* drawing (SPEC-800 sections 8-10)                                     */

void
ML_DrawBorder(int x, int y, int w, int h, int pad, int thick, mcolor_t c, qboolean fill, mcolor_t fillc)
{
	if (fill)
	{
		MR_Fill(x, y, w, pad, fillc);
		MR_Fill(x, y + h, w, -pad, fillc);
		MR_Fill(x, y + pad, pad, h - 2 * pad, fillc);
		MR_Fill(x + w, y + pad, -pad, h - 2 * pad, fillc);
	}

	if (thick != 0 && c != 0)
	{
		int i = (pad - thick) >> 1;
		float x0 = (float)(x + i), y0 = (float)(y + i);
		float x1 = (float)(x + i + w - (pad - thick)), y1 = (float)(y + i + h - (pad - thick));

		MR_Line(x0, y0, x1, y0, (float)thick, c);
		MR_Line(x1, y0, x1, y1, (float)thick, c);
		MR_Line(x1, y1, x0, y1, (float)thick, c);
		MR_Line(x0, y1, x0, y0, (float)thick, c);
	}
}

void
ML_WidgetScreenPos(const mpage_t *pg, const mwidget_t *w, int *x, int *y)
{
	const mframe_t *f = pg->frame;
	const mscreen_t *scr = pg->screen;

	if (w->flags & WF_BOLTED)
	{
		*x = w->boltx + w->bpad + w->xoff;
		*y = w->bolty + w->bpad + w->yoff;
		return;
	}

	*x = f->fx + f->pad + w->bpad + w->x + w->xoff + scr->originx;
	*y = f->fy + f->pad + w->bpad + w->y + w->yoff + scr->originy;

	if (!(w->flags & WF_NOSCROLL))
	{
		*y -= pg->scroll;
	}
}

/* MENU-SCRATCH page zoom: the mouse in the units of widget w of page pg
   (bolted widgets stay in screen units) */
void
ML_PageMouse(const mpage_t *pg, const mwidget_t *w, float *mx, float *my)
{
	*mx = ms.mx;
	*my = ms.my;

	if (pg && pg->zoom > 1.0f && pg->zframe && !(w && (w->flags & WF_BOLTED)))
	{
		*mx = ms.mx / pg->zoom - pg->zdx;
		*my = ms.my / pg->zoom - pg->zdy;
	}
}

/* and back: a point of widget w in screen units */
void
ML_PageToScreen(const mpage_t *pg, const mwidget_t *w, float *x, float *y)
{
	if (pg && pg->zoom > 1.0f && pg->zframe && !(w && (w->flags & WF_BOLTED)))
	{
		*x = (*x + pg->zdx) * pg->zoom;
		*y = (*y + pg->zdy) * pg->zoom;
	}
}

/* MENU-SCRATCH: sx..sh is the page's own rectangle; a stretched picture (the
   full-screen art of the mission-failed, end and loading screens) fills that
   rectangle as on the 640x480 screen and is not pulled out of shape to the
   window, which gets the backdrop colour; tiles and colours fill cx..ch */
static void
ML_DrawBackdrop(mpage_t *pg, int cx, int cy, int cw, int ch, int sx, int sy, int sw, int sh)
{
	int pw, ph, i, j;

	if (!pg->backdrop)
	{
		return;
	}

	/* MENU-SCRATCH (owner t95u): a tiled backdrop of a wide screen (the
	   stock folder art) is replaced by the own background */
	if (pg->bgflat || (pg->bgmode == 1 && pg->screen && pg->screen->wide && MWD_Enabled()))
	{
		MWD_DrawBackground(cx, cy, cw, ch);
		return;
	}

	if (pg->bgcolor)
	{
		MR_Fill(cx, cy, cw, ch, pg->bgcolor);
	}
	else if (pg->bgmode == 2 && pg->bgpic[0] && (sw != cw || sh != ch))
	{
		MR_Fill(cx, cy, cw, ch, 0xFF000000);
	}

	if (!pg->bgpic[0])
	{
		return;
	}

	MR_GetPicSize(&pw, &ph, pg->bgpic);

	if (pw <= 0 || ph <= 0)
	{
		return;
	}

	switch (pg->bgmode)
	{
		case 1: /* tile */
			for (j = 0; j * ph < ch; j++)
			{
				for (i = 0; i * pw < cw; i++)
				{
					MR_DrawPic(cx + i * pw, cy + j * ph, pg->bgpic, 0xFFFFFFFF);
				}
			}

			break;

		case 2: /* stretch */
			MR_StretchPic(sx, sy, sw, sh, 0xFFFFFFFF, pg->bgpic, 0);
			break;

		case 3: /* center */
			MR_DrawPic(cx + (cw - pw) / 2, cy + (ch - ph) / 2, pg->bgpic, 0xFFFFFFFF);
			break;

		case 4: /* left */
			MR_DrawPic(cx, cy, pg->bgpic, 0xFFFFFFFF);
			break;

		case 5: /* right: R.w - pw, not R.x + R.w (original behaviour) */
			MR_DrawPic(cw - pw, cy, pg->bgpic, 0xFFFFFFFF);
			break;
	}
}

/* OpenMerc V1/MENU2 (scr_aspect 1): the default frame is the left-anchored canvas, narrower
   than the window. A rectangle that reaches a side of the canvas is widened to
   the window edge, so backdrops, backfills and borders cover the whole window
   while the content keeps the 4:3 layout. Nothing changes when the canvas is
   the window (scr_aspect 0, or a 4:3 window) or on resize screens.
   OpenMerc MENU2: vertically the canvas stays centred in the virtual window
   when the UI scale is a whole number; horizontally it is left-anchored. */
static void
ML_ExtendToWindow(mscreen_t *scr, int *x, int *y, int *w, int *h)
{
	int x0, x1, y0, y1;

	if (scr->resize || !scr->numframes)
	{
		return;
	}

	x0 = scr->frames[0]->x + scr->originx;
	x1 = x0 + scr->frames[0]->w;
	y0 = scr->frames[0]->y + scr->originy;
	y1 = y0 + scr->frames[0]->h;

	if (*x + *w >= x1)
	{
		*w = MR_VirtualW() - *x;
	}

	if (*x <= x0)
	{
		*w += *x;
		*x = 0;
	}

	if (*y + *h >= y1)
	{
		*h = MR_VirtualH() - *y;
	}

	if (*y <= y0)
	{
		*h += *y;
		*y = 0;
	}
}

static void
ML_DrawPage(mscreen_t *scr, mframe_t *f, mpage_t *pg)
{
	int cx, cy, cw, ch, i;
	int top, bottom;
	mframe_t *pf = pg->frame;
	qboolean zoomed, zon = false;
	int scl[4], pcl[4];

	cx = f->fx + scr->originx;
	cy = f->fy + scr->originy;

	if (pg->global)
	{
		cw = scr->frames[0]->w;
		ch = scr->frames[0]->h;
	}
	else
	{
		cw = f->fw;
		ch = f->fh;
	}

	pg->clip[0] = cx;
	pg->clip[1] = cy;
	pg->clip[2] = cw;
	pg->clip[3] = ch;

	/* MENU2: only the backdrop of a page on the canvas edge reaches the window
	   edges; its models and items stay clipped to the page's frame like on the
	   640x480 screen of 1.07f, which hid the parts placed off-screen (the main
	   menu's side panels and their slide-in start positions) */
	{
		int bx = cx, by = cy, bw = cw, bh = ch;

		if (!f->rail)
		{
			ML_ExtendToWindow(scr, &bx, &by, &bw, &bh);
		}

		if (bx != cx || by != cy || bw != cw || bh != ch)
		{
			MR_SetClip(bx, by, bw, bh);
			ML_DrawBackdrop(pg, bx, by, bw, bh, cx, cy, cw, ch);
		}
		else
		{
			MR_SetClip(cx, cy, cw, ch);
			ML_DrawBackdrop(pg, cx, cy, cw, ch, cx, cy, cw, ch);
		}
	}

	MR_SetClip(cx, cy, cw, ch);
	MM_DrawBackground(pg);

	/* MENU-SCRATCH: chrome and slots of the wide layout (m_wide.c) */
	if (MWD_PageWide(pg))
	{
		MR_ResetClip();
		MWD_DrawChrome(scr);
		MR_SetClip(cx, cy, cw, ch);
		MWD_PlacePage(pg);
	}

	if (pg->autoscroll)
	{
		int L = pg->contenth - (pf->fh - 2 * pf->pad);

		if (L > 0)
		{
			double t = (ms.now - scr->opentime) / 1000.0;
			int s = (int)fmod(t * pg->autorate * 10.0, (double)L);

			if (s < 0)
			{
				s += L;
			}

			pg->scroll = s;
		}
	}

	top = pf->fy + pf->pad + scr->originy;
	bottom = pf->fy + pf->fh - pf->pad + scr->originy;

	/* MENU-SCRATCH page zoom (MP_PageZoom): the widgets of a sparse page are
	   drawn magnified about the frame's corner and centred vertically in it;
	   bolted widgets stay in screen units */
	zoomed = pg->zoom > 1.0f && pf != f && MR_Virtual();
	memcpy(scl, pg->clip, sizeof(scl));

	if (zoomed)
	{
		float z = pg->zoom;
		int ox = f->fx + scr->originx, oy = f->fy + scr->originy;
		int sh = pg->autoscroll ? 0 : (f->fh - (int)(pg->zcontent * z)) / 2;

		if (sh < 0)
		{
			sh = 0;
		}

		pg->zdx = (int)floorf((float)ox / z - (float)ox + 0.5f);
		pg->zdy = (int)floorf((float)(oy + sh) / z - (float)oy + 0.5f);
		pcl[0] = ox;
		pcl[1] = oy;
		pcl[2] = pf->fw;
		pcl[3] = (int)((float)(f->fh - sh) / z);
	}

	for (i = 0; i < pg->numwidgets; i++)
	{
		mwidget_t *w = pg->widgets[i];
		int x, y, th;

		if (zoomed && zon != !(w->flags & WF_BOLTED))
		{
			zon = !zon;

			if (zon)
			{
				MR_ZoomBegin(pg->zdx, pg->zdy, pg->zoom);
				memcpy(pg->clip, pcl, sizeof(pcl));
			}
			else
			{
				MR_ZoomEnd();
				memcpy(pg->clip, scl, sizeof(scl));
			}

			MR_SetClip(pg->clip[0], pg->clip[1], pg->clip[2], pg->clip[3]);
		}

		if ((w->flags & WF_BOLTED) && !w->omslot)
		{
			MM_UpdateBolt(pg, w);
		}

		if (w->cls->update)
		{
			w->cls->update(pg, w);
		}

		if (w->omslot)
		{
			MWD_PlaceSlot(pg, w);
		}

		ML_WidgetScreenPos(pg, w, &x, &y);
		th = ML_TotalH(w);

		if (!(w->flags & WF_BOLTED))
		{
			int ty = y - w->bpad - w->yoff;

			if (ty + th <= top || ty >= bottom)
			{
				if (!(w->flags & WF_NOTPLACED))
				{
					continue;
				}
			}
		}

		w->flags &= ~WF_HIDEBORDER;

		if (w->omslot && MWD_DrawSlot(pg, w, x, y))
		{
			continue;
		}

		if (w->cls->draw)
		{
			w->cls->draw(pg, w, x, y);
		}

		if ((w->flags & WF_HASBORDER) && !(w->flags & WF_HIDEBORDER))
		{
			ML_DrawBorder(x - w->bpad, y - w->bpad, ML_TotalW(w), ML_TotalH(w), w->bpad, w->bthick,
					w->bcolor, false, 0);
		}
	}

	if (zon)
	{
		MR_ZoomEnd();
	}

	memcpy(pg->clip, scl, sizeof(scl));
	MR_ResetClip();
}

void
ML_DrawFrame(mscreen_t *scr, mframe_t *f)
{
	mpage_t *pg = f->numpages ? f->pages[f->numpages - 1] : NULL;
	qboolean chrome = false;

	/* MENU-SCRATCH: wide layout chrome under the page instead of the line border */
	if (f->rail && MWD_Enabled())
	{
		MWD_Chrome("panel", f->x + scr->originx, f->y + scr->originy, f->w, f->h, 0);
		chrome = true;
	}
	else if (MWD_FrameWell(scr, f, f->x + scr->originx, f->y + scr->originy, f->w, f->h))
	{
		chrome = true;
	}

	if (pg)
	{
		ML_DrawPage(scr, f, pg);
	}

	if (f->border && !chrome)
	{
		int bx = f->x + scr->originx, bw = f->w;
		int by = f->y + scr->originy, bh = f->h;

		if (!f->rail)
		{
			ML_ExtendToWindow(scr, &bx, &by, &bw, &bh);
		}

		ML_DrawBorder(bx, by, bw, bh, f->pad, f->thick, f->bcolor,
				pg == NULL, f->backfill);
	}
}
