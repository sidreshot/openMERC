/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc MENU-SCRATCH: the openMERC wide layout of the folder screens
 * (docs/impl/MENU2.md "openMERC wide layout", docs/DEVIATIONS.md).
 *
 * The stock folder screens are a 640x480 composition around the Ghoul model
 * web_folders: the tabs, the title, the ticker and the three control buttons
 * are bolted to its parts and the content frame "main" is a fixed 560x292
 * hole in it. With menu_layout 1 and the virtual window (MR_Virtual) the
 * screen is laid out for the whole window instead:
 *
 *   nav panel | tab strip (folder tabs drawn from the bolted tab images)
 *             | panel: title band, the content frame "main" across the
 *             |        panel's full width and height, footer with the
 *             |        ticker and the control buttons as labelled buttons
 *
 * The web_folders model and the decoration models bolted to it are not
 * created; every widget the stock page bolts to one of its parts gets a slot
 * of this layout instead (MWD_BoltSlot), so the stock RMF files, their key
 * lists and tooltips are used unchanged. The chrome is own flat drawing
 * (MWD_Chrome, no pictures); the tab icons of the stock pages are not drawn.
 */

#include "header/menu.h"
#include "header/m_draw.h"

/* layout metrics, menu units */
#define MWD_GAP 8          /* nav panel to tab strip */
#define MWD_TABH 30        /* tab strip */
#define MWD_RIM 9          /* panel rim (padding inside the panel border) */
#define MWD_INNER 14       /* panel edge to content */
#define MWD_TITLEH 24
#define MWD_FOOTH 24
#define MWD_NAVMIN 600     /* least content column for the nav panel */

enum
{
	OMS_NONE,
	OMS_HIDE,
	OMS_TITLE,
	OMS_TICKER,
	OMS_HELP,      /* NULL_BACK: page help (gfxinfo, chat help) */
	OMS_HOME,      /* NULL_HOME_PAGE */
	OMS_GAME,      /* NULL_GAME: return to the game */
	OMS_RETURN,    /* NULL_HELP: popmenu */
	OMS_DEPLOY,    /* null_arrow_deploy */
	OMS_TAB = 16   /* + tab number */
};

static cvar_t *mwd_layout;

qboolean
MWD_Enabled(void)
{
	if (!mwd_layout)
	{
		mwd_layout = Cvar_Get("menu_layout", "1", CVAR_ARCHIVE);
	}

	return MR_Virtual() && mwd_layout->value != 0;
}

/* screens drawn with the wide layout: the folder screens (a "topic" frame) */
qboolean
MWD_ScreenWide(const char *name)
{
	static const char *never[] = {"outfit", "mail", "testmotd", "objectives", NULL};
	char *buf = NULL;
	qboolean yes = false;
	int i, len;

	if (!MWD_Enabled())
	{
		return false;
	}

	for (i = 0; never[i]; i++)
	{
		if (!Q_stricmp(name, never[i]))
		{
			return false;
		}
	}

	len = FS_LoadFile(va("menus/%s.rmf", name), (void **)&buf);

	if (buf && len > 0)
	{
		char *t = Z_Malloc(len + 1);

		memcpy(t, buf, len);
		t[len] = 0;
		Q_strlwr(t);
		yes = strstr(t, "<frame topic ") != NULL;
		Z_Free(t);
	}

	if (buf)
	{
		FS_FreeFile(buf);
	}

	return yes;
}

/* the nav panel fits next to a content column of MWD_NAVMIN */
qboolean
MWD_NavFits(void)
{
	return MR_VirtualW() >= 2 * MWD_MARGIN + MWD_NAVW + MWD_GAP + MWD_NAVMIN;
}

static void
MWD_Rect(mrect_t *r, int x, int y, int w, int h)
{
	r->x = x;
	r->y = y;
	r->w = w > 0 ? w : 0;
	r->h = h > 0 ? h : 0;
}

void
MWD_Layout(mscreen_t *scr, qboolean nav)
{
	mwgeom_t *g = &scr->wg;
	int VW = MR_VirtualW(), VH = MR_VirtualH();
	int cx = nav ? MWD_MARGIN + MWD_NAVW + MWD_GAP : MWD_MARGIN;
	int top, bottom;

	MWD_Rect(&g->nav, MWD_MARGIN, MWD_MARGIN, MWD_NAVW, VH - 2 * MWD_MARGIN);
	MWD_Rect(&g->col, cx, MWD_MARGIN, VW - MWD_MARGIN - cx, VH - 2 * MWD_MARGIN);
	MWD_Rect(&g->panel, g->col.x, g->col.y + MWD_TABH, g->col.w, g->col.h - MWD_TABH);
	MWD_Rect(&g->title, g->panel.x + MWD_INNER, g->panel.y + MWD_RIM + 2,
		g->panel.w - 2 * MWD_INNER, MWD_TITLEH);
	MWD_Rect(&g->foot, g->panel.x + MWD_INNER, g->panel.y + g->panel.h - MWD_RIM - 3 - MWD_FOOTH,
		g->panel.w - 2 * MWD_INNER, MWD_FOOTH);
	/* the ticker has its own full-width lane above the buttons: beside them
	   a long tip was cut (worst at 4:3, where the buttons take most of the row) */
	MWD_Rect(&g->tick, g->foot.x, g->foot.y - 4 - MWD_FOOTH, g->foot.w, MWD_FOOTH);
	top = g->title.y + g->title.h + 6;
	bottom = g->tick.y - 6;
	MWD_Rect(&g->main, g->panel.x + MWD_INNER, top, g->panel.w - 2 * MWD_INNER, bottom - top);

	/* horizontal factor of the stock 560-wide main area: <tab> stops and cut
	   frames of the pages inside it widen with it */
	g->sx = g->main.w / 560.0f;
}

/* default frame of a wide screen: the content column */
void
MWD_DefaultFrame(mscreen_t *scr, mframe_t *f)
{
	f->x = f->fx = scr->wg.col.x;
	f->y = f->fy = scr->wg.col.y;
	f->w = f->fw = scr->wg.col.w;
	f->h = f->fh = scr->wg.col.h;
}

/*
 * <frame> of a wide screen (Tag_Frame, before the cut). topic: the tab strip;
 * main: the panel's content rectangle without the stock 40-unit pad. The
 * frames the stock screen cuts out of main with a fixed size (join, start,
 * chat, motd) keep their share of the stock 560x292 content area.
 * The 1.07f GameSpy patch (gs.pak join.rmf) splits the stock topic frame
 * into a "topicarea" with the topic and a GameSpy logo beside it; the topic
 * still becomes the tab strip and the rest of that area (the logo of a
 * service that no longer exists) is dropped: *nopage tells the caller not to
 * show the frame's page.
 * Returns true when the frame is placed (no cut).
 */
qboolean
MWD_Frame(mscreen_t *scr, mframe_t *f, const char *cut, int W, int H, qboolean *nopage)
{
	const mwgeom_t *g = &scr->wg;
	mframe_t *main;

	if (!scr->wide)
	{
		return false;
	}

	if ((!cut[0] || !Q_stricmp(cut, "topicarea")) && Q_stricmp(f->name, "topic") &&
		(!Q_stricmp(f->name, "topicarea") || !Q_stricmp(cut, "topicarea")))
	{
		f->x = f->fx = g->col.x;
		f->y = f->fy = g->col.y;
		f->w = f->fw = 0;
		f->h = f->fh = 0;
		f->pad = 0;
		f->thick = 0;
		f->border = false;
		*nopage = true;
		return true;
	}

	if ((!cut[0] || !Q_stricmp(cut, "topicarea")) && !Q_stricmp(f->name, "topic"))
	{
		f->x = f->fx = g->col.x;
		f->y = f->fy = g->col.y;
		f->w = f->fw = g->col.w;
		f->h = f->fh = MWD_TABH;
		f->pad = 0;
		return true;
	}

	if (!cut[0] && !Q_stricmp(f->name, "main"))
	{
		f->x = f->fx = g->main.x;
		f->y = f->fy = g->main.y;
		f->w = f->fw = g->main.w;
		f->h = f->fh = g->main.h;
		f->pad = 0;
		f->thick = 0;
		return true;
	}

	main = MP_FindFrame(scr, "main");

	if (main && !Q_stricmp(cut, "main"))
	{
		if (W >= 1 && f->w >= 1)
		{
			f->w = f->w * g->main.w / 560;
		}

		if (W == 0 && H >= 1 && f->h > 32)
		{
			f->h = f->h * g->main.h / 292;
		}
	}

	return false;
}

/* ------------------------------------------------------------------ */
/* drawing                                                              */

/* MENU-SCRATCH (owner t95u): own flat chrome, drawn with fills on whole
   window pixels (MR_FlatBox): no pictures, crisp at every scale. Colours
   0xAABBGGRR: slate panels, a thin steel border, the orange accent of the
   menu text. hover != 0 is the hovered state of tabs and buttons */
#define MWD_ACCENT 0xFF2080FF
#define MWD_EDGE 0xFF463A30

void
MWD_Chrome(const char *name, int x, int y, int w, int h, int hover)
{
	/* over the live game (MWD_DrawBackground) the panels let it show through */
	qboolean live = cls.state == ca_active && Cvar_VariableValue("menu_livebg");

	if (!strcmp(name, "panel"))
	{
		MR_FlatBox(x, y, w, h, live ? 0x981E1912 : 0xEE1E1912, MWD_EDGE, MWD_ACCENT, false);
	}
	else if (!strcmp(name, "well"))
	{
		MR_FlatBox(x, y, w, h, 0xB0100D0A, 0xFF332A23, 0, false);
	}
	else if (!strcmp(name, "tab"))
	{
		MR_FlatBox(x, y, w, h, hover ? 0xF0383026 : 0xE6241E18, MWD_EDGE, 0, false);
	}
	else if (!strcmp(name, "tab_on"))
	{
		MR_FlatBox(x, y, w, h, live ? 0x981E1912 : 0xEE1E1912, MWD_EDGE, MWD_ACCENT, true);
	}
	else if (!strcmp(name, "button"))
	{
		MR_FlatBox(x, y, w, h, hover ? 0xF0403628 : 0xF02A231C, hover ? MWD_ACCENT : 0xFF54473B, 0, false);
	}
	else if (!strcmp(name, "groove"))
	{
		MR_FlatBox(x, y, w, h, 0xC00C0A08, 0xFF332A23, 0, false);
	}
}

/* the backdrop of a wide screen instead of the stock tiled picture: the
   live (dimmed) game while a level runs (cl_screen.c draws it under the menu,
   menu_livebg), else an own dark gradient */
void
MWD_DrawBackground(int x, int y, int w, int h)
{
	if (cls.state == ca_active && Cvar_VariableValue("menu_livebg"))
	{
		MR_Fill(x, y, w, h, 0x60000000);
		return;
	}

	MR_FlatGradient(x, y, w, h, 0xFF2A221C, 0xFF0E0B09);
}

/* chrome under the folder screen's widgets: drawn by the topic page after
   its backdrop (ML_DrawPage) */
void
MWD_DrawChrome(mscreen_t *scr)
{
	const mwgeom_t *g = &scr->wg;

	/* the nav panel's chrome is drawn with its frame (ML_DrawFrame) */
	MWD_Chrome("panel", g->panel.x, g->panel.y, g->panel.w, g->panel.h, 0);

	/* title band divider */
	MR_FlatBox(g->title.x, g->title.y + g->title.h + 1, g->title.w, 1, MWD_EDGE, 0, 0, false);
}

/* a framed page area of a wide screen (a <frame> with a visible border):
   an inset well instead of the 1-pixel stock line */
qboolean
MWD_FrameWell(mscreen_t *scr, mframe_t *f, int x, int y, int w, int h)
{
	if (!scr->wide || f->rail || !f->border || f->thick <= 0)
	{
		return false;
	}

	MWD_Chrome("well", x, y, w, h, 0);
	return true;
}

/* ------------------------------------------------------------------ */
/* slots                                                                */

/* a page of a wide screen in its tab strip frame */
qboolean
MWD_PageWide(const mpage_t *pg)
{
	return pg && pg->screen && pg->screen->wide && pg->frame &&
		!Q_stricmp(pg->frame->name, "topic");
}

/* bolt / bbolt option of a widget on such a page */
qboolean
MWD_BoltSlot(mparse_t *p, mwidget_t *w, const char *name)
{
	static const struct { const char *name; int slot; } map[] = {
		{"null_header", OMS_TITLE}, {"null_ticker_tape", OMS_TICKER},
		{"null_home_page", OMS_HOME}, {"null_game", OMS_GAME}, {"null_help", OMS_RETURN},
		{"null_back", OMS_HELP}, {"null_arrow_deploy", OMS_DEPLOY},
		{NULL, 0}
	};
	int i;

	(void)p;
	w->omslot = OMS_HIDE;

	if (!Q_strncasecmp(name, "null_tab", 8) && atoi(name + 8) > 0)
	{
		w->omslot = OMS_TAB + atoi(name + 8);
	}
	else
	{
		for (i = 0; map[i].name; i++)
		{
			if (!Q_stricmp(name, map[i].name))
			{
				w->omslot = map[i].slot;
				break;
			}
		}
	}

	w->boltmodel = NULL;
	w->bolt = 0;
	w->flags |= WF_BOLTED;
	return true;
}

/* own short labels (strings menu_omset, tools/menu2/gen_strings.py) */
static const char *
MWD_String(const char *key)
{
	const char *s = MSP_Find(key);

	return (s && s[0]) ? s : "";
}

static const char *
MWD_TabLabel(const mpage_t *pg, const mwidget_t *w)
{
	static const struct { const char *icon, *key; } map[] = {
		{"f_game", "MENU_OMSET_RAIL_NEWGAME"}, {"f_load", "MENU_OMSET_RAIL_LOAD"},
		{"f_save", "MENU_OMSET_RAIL_SAVE"}, {"f_credits", "MENU_OMSET_RAIL_CREDITS"},
		{"f_player", "MENU_OMSET_TAB_PLAYER"}, {"f_join", "MENU_OMSET_TAB_JOIN"},
		{"f_start", "MENU_OMSET_TAB_START"}, {"f_options", "MENU_OMSET_TAB_OPTIONS"},
		{"f_motd", "MENU_OMSET_TAB_MOTD"}, {"f_chat", "MENU_OMSET_TAB_CHAT"},
		{"f_keys", "MENU_OMSET_RAIL_KEYS"}, {"f_joystick", "MENU_OMSET_TAB_GAMEPAD"},
		{"f_video", "MENU_OMSET_RAIL_VIDEO"}, {"f_audio", "MENU_OMSET_RAIL_AUDIO"},
		{"f_scale", "MENU_OMSET_RAIL_GAMEPLAY"}, {"f_misc", "MENU_OMSET_TAB_MOUSE"},
		{"f_stats", "MENU_OMSET_RAIL_STATS"}, {"f_lock", "MENU_OMSET_TAB_LOCK"},
		{"f_dmflags", "MENU_OMSET_TAB_RULES"},
		{NULL, NULL}
	};
	const char *pic = MW_ImagePic(w), *base;
	int i;

	/* MENU-SCRATCH: own folder pages name their tabs (<image none label "...">) */
	if (MW_ImageLabel(w))
	{
		return MW_ImageLabel(w);
	}

	if (!pic)
	{
		return "";
	}

	base = strrchr(pic, '/');
	base = base ? base + 1 : pic;

	/* MENU-SCRATCH: the main menu's one tab (menus/t_omhome.rmf) */
	if (!Q_stricmp(pg->screen->name, "main"))
	{
		return MWD_String("MENU_OMSET_HOME_TAB");
	}

	/* the keys family uses f_options for the language tab; the bots
	   screens use f_dmflags for the bot list */
	if (!Q_stricmp(base, "f_options") && (!Q_stricmp(pg->screen->name, "keys") ||
		!Q_stricmp(pg->screen->name, "joystick") || !Q_stricmp(pg->screen->name, "language")))
	{
		return MWD_String("MENU_OMSET_TAB_LANGUAGE");
	}

	if (!Q_stricmp(base, "f_dmflags") && w->tip && MSP_Find("BOT_MENU_PICK_A_BOT") &&
		!strcmp(w->tip, MSP_Find("BOT_MENU_PICK_A_BOT")))
	{
		return MWD_String("MENU_OMSET_TAB_BOTS");
	}

	for (i = 0; map[i].icon; i++)
	{
		if (!Q_stricmp(base, map[i].icon))
		{
			return MWD_String(map[i].key);
		}
	}

	return "";
}

static const char *
MWD_ButtonLabel(int slot)
{
	switch (slot)
	{
		case OMS_HELP: return MWD_String("MENU_OMSET_WIDE_HELP");
		case OMS_HOME: return MWD_String("MENU_OMSET_RAIL_MAIN");
		case OMS_GAME: return MWD_String("MENU_OMSET_WIDE_RESUME");
		case OMS_RETURN: return MWD_String("MENU_OMSET_WIDE_BACK");
		case OMS_DEPLOY: return MWD_String("MENU_OMSET_WIDE_DEPLOY");
		default: return "";
	}
}

#define MWD_TABPAD 14      /* tab label padding */
#define MWD_LABELFONT "medium"

static qboolean
MWD_TabCurrent(const mwidget_t *w)
{
	return w->binds == NULL;
}

static void
MWD_Hide(mwidget_t *w)
{
	w->omrect[0] = -10000;
	w->omrect[1] = -10000;
	w->omrect[2] = 1;
	w->omrect[3] = 1;
}

/* positions of every slot of a page, once per frame before its widgets */
void
MWD_PlacePage(mpage_t *pg)
{
	const mwgeom_t *g = &pg->screen->wg;
	mwidget_t *tabs[32];
	int ntabs = 0, i, j, x, total, avail;
	qboolean iconsonly = false;
	int bw[8], nb = 0, bx;
	mwidget_t *buttons[8];
	static const int order[] = {OMS_HELP, OMS_DEPLOY, OMS_HOME, OMS_GAME, OMS_RETURN};
	qboolean ingame = cls.state == ca_active;
	int fh = MR_FontHeight(MWD_LABELFONT);

	/* tabs, in bolt order */
	for (i = 0; i < pg->numwidgets && ntabs < 32; i++)
	{
		if (pg->widgets[i]->omslot >= OMS_TAB)
		{
			tabs[ntabs++] = pg->widgets[i];
		}
	}

	for (i = 1; i < ntabs; i++)
	{
		for (j = i; j > 0 && tabs[j - 1]->omslot > tabs[j]->omslot; j--)
		{
			mwidget_t *t = tabs[j];
			tabs[j] = tabs[j - 1];
			tabs[j - 1] = t;
		}
	}

	avail = g->col.w - 2 * MWD_INNER;

	for (total = 0, i = 0; i < ntabs; i++)
	{
		total += 2 * MWD_TABPAD + MR_StringWidth(MWD_TabLabel(pg, tabs[i]), MWD_LABELFONT) + 4;
	}

	iconsonly = total > avail;
	x = g->col.x + MWD_INNER;

	for (i = 0; i < ntabs; i++)
	{
		mwidget_t *w = tabs[i];
		/* too many tabs: the small font and narrow padding */
		int tw = iconsonly ? 12 + MR_StringWidth(MWD_TabLabel(pg, w), "small") :
			2 * MWD_TABPAD + MR_StringWidth(MWD_TabLabel(pg, w), MWD_LABELFONT);

		w->omrect[0] = x;
		w->omrect[1] = g->col.y + 2;
		w->omrect[2] = tw;
		w->omrect[3] = MWD_TABH - 2 + (MWD_TabCurrent(w) ? MWD_RIM + 1 : 0);
		x += tw + 4;
	}

	/* footer buttons, right-aligned */
	for (i = 0; i < pg->numwidgets; i++)
	{
		mwidget_t *w = pg->widgets[i];

		if (w->omslot == OMS_HIDE || (w->omslot == OMS_GAME && !ingame))
		{
			MWD_Hide(w);
		}
	}

	for (j = 0; j < (int)(sizeof(order) / sizeof(order[0])); j++)
	{
		for (i = 0; i < pg->numwidgets && nb < 8; i++)
		{
			mwidget_t *w = pg->widgets[i];

			if (w->omslot == order[j] && w->text == NULL &&
				!(w->omslot == OMS_GAME && !ingame))
			{
				buttons[nb] = w;
				bw[nb] = MR_StringWidth(MWD_ButtonLabel(w->omslot), MWD_LABELFONT) + 24;
				bw[nb] = bw[nb] < 64 ? 64 : bw[nb];
				nb++;
			}
			else if (w->omslot == order[j] && w->text)
			{
				/* a text bolted to a button part (outfit "deploy" label) */
				MWD_Hide(w);
			}
		}
	}

	bx = g->foot.x + g->foot.w;

	for (i = nb - 1; i >= 0; i--)
	{
		bx -= bw[i];
		buttons[i]->omrect[0] = bx;
		buttons[i]->omrect[1] = g->foot.y;
		buttons[i]->omrect[2] = bw[i];
		buttons[i]->omrect[3] = MWD_FOOTH;
		bx -= 6;
	}

	/* title and ticker */
	for (i = 0; i < pg->numwidgets; i++)
	{
		mwidget_t *w = pg->widgets[i];

		if (w->omslot == OMS_TITLE)
		{
			int tw = (int)(w->w * w->scale), th = (int)(w->h * w->scale);

			w->omrect[0] = g->title.x + (g->title.w - tw) / 2;
			w->omrect[1] = g->title.y + (g->title.h - th) / 2;
			w->omrect[2] = w->w;
			w->omrect[3] = w->h;
		}
		else if (w->omslot == OMS_TICKER)
		{
			w->omrect[0] = g->tick.x + 8;
			w->omrect[1] = g->tick.y + (MWD_FOOTH - (int)(w->h * w->scale)) / 2;
			w->omrect[2] = g->tick.w - 16;
			w->omrect[3] = w->h;
		}
	}

	(void)fh;
}

/* a slot widget's rectangle, after its class update (which may resize it) */
void
MWD_PlaceSlot(mpage_t *pg, mwidget_t *w)
{
	(void)pg;

	w->bpad = 0;
	w->xoff = 0;
	w->yoff = 0;
	w->boltx = w->omrect[0];
	w->bolty = w->omrect[1];
	w->boltfacing = true;

	if (w->omslot == OMS_TITLE)
	{
		/* centre again with the size after the update (ctext) */
		const mwgeom_t *g = &pg->screen->wg;
		int tw = (int)(w->w * w->scale);

		w->boltx = g->title.x + (g->title.w - tw) / 2;
		return;
	}

	if (w->omslot == OMS_TICKER)
	{
		w->w = w->omrect[2];
		return;
	}

	if (w->omslot >= OMS_TAB || (w->omslot >= OMS_HELP && w->omslot <= OMS_DEPLOY))
	{
		w->w = w->omrect[2];
		w->h = w->omrect[3];
		w->scale = 1.0f;
	}
}

/* own drawing of tab and button slots; false = the class draws it */
qboolean
MWD_DrawSlot(mpage_t *pg, mwidget_t *w, int x, int y)
{
	qboolean hover = (w->flags & WF_HOVER) != 0;

	if (w->omslot == OMS_HIDE || w->omrect[0] <= -10000)
	{
		return true;
	}

	if (w->omslot >= OMS_TAB)
	{
		const char *label = MWD_TabLabel(pg, w);
		qboolean cur = MWD_TabCurrent(w);
		const char *font = (w->w < 2 * MWD_TABPAD + MR_StringWidth(label, MWD_LABELFONT)) ?
			"small" : MWD_LABELFONT;
		int fh = MR_FontHeight(font), tw = MR_StringWidth(label, font);

		/* own flat tab; the stock tab icons (retail art) are not drawn */
		MWD_Chrome(cur ? "tab_on" : "tab", x, y, w->w, w->h, hover && !cur);

		if (label[0])
		{
			MR_DrawString(x + (w->w - tw) / 2, y + (MWD_TABH - 2 - fh) / 2 + 1, label,
				cur ? 0xFF60C0FF : (hover ? 0xFFFFFFFF : 0xFFB8C4C0), font, 0);
		}

		return true;
	}

	if (w->omslot >= OMS_HELP && w->omslot <= OMS_DEPLOY)
	{
		const char *label = MWD_ButtonLabel(w->omslot);
		int fh = MR_FontHeight(MWD_LABELFONT);
		int tw = MR_StringWidth(label, MWD_LABELFONT);

		MWD_Chrome("button", x, y, w->w, w->h, hover);
		MR_DrawString(x + (w->w - tw) / 2, y + (w->h - fh) / 2 + 1, label,
			hover ? 0xFFFFFF60 : 0xFFD8E0DC, MWD_LABELFONT, 0);
		return true;
	}

	if (w->omslot == OMS_TICKER)
	{
		const mwgeom_t *g = &pg->screen->wg;

		MWD_Chrome("groove", g->tick.x, g->tick.y, g->tick.w, MWD_FOOTH, 0);
	}

	return false;
}
