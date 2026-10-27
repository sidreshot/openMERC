/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc: Soldier of Fortune menu system - opening and closing, the stack
 * of menu screens and its console commands, the menu command queue, the
 * per-frame loop, keys, mouse, wheel, TAB navigation, tooltips, the mouse
 * pointer and the entry points the rest of the client calls (M_*).
 *
 * This replaces the Quake II menu (menu.c, qmenu.c, videomenu.c of Yamagi
 * Quake II): every menu is an .rmf page (SPEC-130, SPEC-800).
 *
 * Implements: SPEC-801, SPEC-800 (screens), SPEC-130
 */

#include "header/menu.h"
#include "../../common/header/om_version.h"
#include "header/m_draw.h"
#include "../../common/header/sof/engine.h"
#include "../../common/header/gamespy_chat.h" /* OpenMerc (I2d): MC_ClearLobby */

menustate_t ms;

cvar_t *menu_mouse_x, *menu_mouse_y;
cvar_t *m_cursor_speed, *m_cursor_accel, *m_sensitivity_wheel;
cvar_t *tip_duration, *cursor_blink_speed, *ghl_no_texture_stack;
cvar_t *scr_menuoverlay, *m_doublespeed;
cvar_t *menu_dialogdim; /* MENU2: darkening of the screen under a dialog, 0..1 */
cvar_t *menu_gamepad;   /* MENU2: gamepad buttons navigate the menus */

/* ------------------------------------------------------------------ */
/* small helpers                                                        */

int
M_Time(void)
{
	return ms.drawing ? ms.now : Sys_Milliseconds();
}

void
M_PlaySound(const char *name)
{
	S_StartLocalSound(name);
}

void
M_ExecNow(const char *text)
{
	char buf[MENU_MAX_TOKEN + 64];

	Q_strlcpy(buf, text, sizeof(buf));
	Cmd_ExecuteString(buf);
}

qboolean
M_KeyIsDown(int key)
{
	return key >= 0 && key < K_LAST && keydown[key];
}

static qboolean
M_ShiftDown(void)
{
	return keydown[K_LSHIFT] || keydown[K_RSHIFT];
}

static qboolean
M_AltDown(void)
{
	return keydown[K_LALT] || keydown[K_RALT];
}

static qboolean
M_CtrlDown(void)
{
	return keydown[K_LCTRL] || keydown[K_RCTRL];
}

mscreen_t *
M_TopScreen(void)
{
	return ms.depth > 0 ? ms.stack[ms.depth - 1] : NULL;
}

void
M_MarkTabDirty(mscreen_t *scr)
{
	if (scr)
	{
		scr->tabdirty = true;
	}
}

static mpage_t *
M_FramePage(mframe_t *f)
{
	return f->numpages ? f->pages[f->numpages - 1] : NULL;
}

/* ------------------------------------------------------------------ */
/* menu command queue (SPEC-801 section 5)                              */

#define MQ_MAX 512

static char *mq_cmds[MQ_MAX];
static int mq_count;

void
M_QueueCommand(const char *text, qboolean forced)
{
	char part[MENU_MAX_TOKEN];
	const char *s = text;

	if (!forced && mq_count > 0)
	{
		return; /* a non-forced request is discarded while the queue is busy */
	}

	while (*s)
	{
		int n = 0;

		while (*s && *s != ';' && *s != '\r' && *s != '\n')
		{
			if (n < (int)sizeof(part) - 1)
			{
				part[n++] = *s;
			}

			s++;
		}

		if (*s)
		{
			s++;
		}

		part[n] = 0;

		/* empty parts are dropped */
		{
			const char *t = part;

			while (*t == ' ' || *t == '\t')
			{
				t++;
			}

			if (!*t)
			{
				continue;
			}
		}

		if (mq_count < MQ_MAX)
		{
			mq_cmds[mq_count++] = CopyString(part);
		}
	}
}

static void
M_RunQueue(void)
{
	if (mq_count <= 0)
	{
		return;
	}

	Cbuf_AddText(mq_cmds[0]);
	Cbuf_AddText("\n");
	Z_Free(mq_cmds[0]);
	memmove(mq_cmds, mq_cmds + 1, (mq_count - 1) * sizeof(char *));
	mq_count--;
}

/* ------------------------------------------------------------------ */
/* screens                                                              */

/* OpenMerc V1: window size and scr_aspect the screens were laid out for */
static int m_layoutw, m_layouth, m_layoutaspect;
static float m_layoutscale;

/*
 * OpenMerc MENU-SCRATCH (docs/impl/MENU2.md "Widescreen composition"): in the
 * virtual window the default frame is the untouched 640x480 SoF canvas,
 * centred vertically and centred horizontally inside the composition - the
 * canvas alone, or the navigation rail (MENU_RAIL_W, then a gap) followed by
 * the canvas. Every stock frame, model and bolt keeps its 640x480 coordinates
 * and one uniform factor; only the tile backdrop reaches the window edges
 * (ML_ExtendToWindow).
 */

/* screens that carry the rail: the topic-folder screens of the stock menus
   and the main menu; dialogs, loading, outfit and the end screens do not */
static qboolean
M_ScreenWantsRail(const char *name)
{
	static const char *always[] = {"main", "player", NULL};
	static const char *never[] = {"outfit", "mail", "testmotd", "objectives", NULL};
	char *buf = NULL;
	qboolean yes = false;
	int i, len;

	if (!MR_RailShown())
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

	for (i = 0; always[i]; i++)
	{
		if (!Q_stricmp(name, always[i]))
		{
			/* wide layout: the 640 canvas of these screens needs room right of
			   the nav panel with a margin on both sides; at 2560x1440 (853
			   units) it does not get it and touched the panel and the window
			   edge, so the screen is the centred canvas alone (it carries its
			   own navigation) */
			return !MWD_Enabled() || MR_VirtualW() >= 3 * MWD_MARGIN + MWD_NAVW + 640;
		}
	}

	len = FS_LoadFile(va("menus/%s.rmf", name), (void **)&buf);

	if (buf && len > 0)
	{
		char *t = Z_Malloc(len + 1);

		memcpy(t, buf, len);
		t[len] = 0;
		Q_strlwr(t);
		yes = strstr(t, "<frame topic ") != NULL || strstr(t, "<frame topicarea ") != NULL;
		Z_Free(t);
	}

	if (buf)
	{
		FS_FreeFile(buf);
	}

	return yes;
}

/* window x (menu units) of the 640-wide canvas */
static int
M_CanvasX(qboolean rail)
{
	if (MWD_Enabled())
	{
		/* wide layout: the canvas (main menu, player) centred right of the nav panel */
		int left = rail ? MWD_MARGIN + MWD_NAVW : 0;
		int room = MR_VirtualW() - left - (rail ? MWD_MARGIN : 0) - 640;

		return left + (room > 0 ? room / 2 : 0);
	}

	int comp = 640 + (rail ? MENU_COMP_RAIL : 0);
	int x0 = (MR_VirtualW() - comp) / 2;

	return x0 + (rail ? MENU_COMP_RAIL : 0);
}

/* MENU-SCRATCH: centre x of the panel a dialog (stm resize) opens over */
int
M_DialogCentreX(mscreen_t *scr)
{
	int i;

	for (i = ms.depth - 1; i >= 0; i--)
	{
		mscreen_t *s = ms.stack[i];

		if (s != scr && !s->resize && s->numframes)
		{
			return s->frames[0]->x + s->frames[0]->w / 2;
		}
	}

	return MR_VirtualW() / 2;
}

/* rail row of the screen (menus/om_rail.rmf, menu_omrail_cur) */
static int
M_RailCurrent(const char *name)
{
	static const struct { const char *name; int row; } map[] = {
		{"game", 1}, {"game2", 1}, {"game3", 1}, {"load", 2}, {"save", 3}, {"credits", 4},
		{"player", 5}, {"player2", 5}, {"join", 6}, {"advjoin", 6}, {"socks", 6}, {"motd", 6}, {"start", 7},
		{"bots", 7}, {"pickbot", 7}, {"dmflags", 7}, {"options", 8}, {"chat", 8},
		{"keys", 9}, {"joystick", 9}, {"video", 10}, {"audio", 11}, {"scale", 12},
		{"misc", 13}, {"language", 13}, {"stats", 14},
		{NULL, 0}
	};
	int i;

	for (i = 0; map[i].name; i++)
	{
		if (!Q_stricmp(name, map[i].name))
		{
			return map[i].row;
		}
	}

	return 0;
}

/* MENU-SCRATCH: the rail frame left of the canvas, after the screen's own page */
static void
M_AddRail(mscreen_t *scr)
{
	mframe_t *f, *c = scr->frames[0];

	if (!scr->hasrail || scr->resize || scr->numframes >= MENU_MAX_FRAMES)
	{
		return;
	}

	f = Z_Malloc(sizeof(*f));
	Q_strlcpy(f->name, "omrail", sizeof(f->name));
	f->screen = scr;
	f->rail = true;
	f->x = f->fx = c->x - MENU_COMP_RAIL;
	f->y = f->fy = c->y + 20;
	f->w = f->fw = MENU_RAIL_W;
	f->h = f->fh = 440;
	f->border = true;
	f->pad = 7;                /* 7 + 176-wide rows + 7 = MENU_RAIL_W */

	if (MWD_Enabled())
	{
		/* wide layout: the nav panel at the window's left edge, full height,
		   in the panel chrome (ML_DrawFrame) */
		f->x = f->fx = MWD_MARGIN;
		f->y = f->fy = MWD_MARGIN;
		f->w = f->fw = MWD_NAVW;
		f->h = f->fh = MR_VirtualH() - 2 * MWD_MARGIN;
		f->pad = MWD_NAVPAD;  /* 14 + 176-wide rows + 14 = MWD_NAVW */
	}
	f->thick = 1;
	f->bcolor = 0xFF808080;    /* stock tint "gray" */
	f->backfill = 0xBF000000;  /* stock tint "smokey" */
	scr->frames[scr->numframes++] = f;

	Cvar_SetValue("menu_omrail_cur", (float)M_RailCurrent(scr->name));

	if (!MP_ShowPage(scr, f, "om_rail", false))
	{
		scr->numframes--;
		Z_Free(f);
	}
}

static int m_layoutw, m_layouth, m_layoutaspect, m_layoutrail, m_layoutwide;
static float m_layoutscale;

static mscreen_t *
M_NewScreen(const char *name)
{
	mscreen_t *scr = Z_Malloc(sizeof(*scr));
	mframe_t *f = Z_Malloc(sizeof(*f));

	m_layoutw = viddef.width;
	m_layouth = viddef.height;
	m_layoutaspect = SCR_AspectCorrect() ? 1 : 0;
	m_layoutscale = MR_Scale();
	m_layoutrail = MR_RailShown() ? 1 : 0;
	m_layoutwide = MWD_Enabled() ? 1 : 0;

	Q_strlcpy(scr->name, name, sizeof(scr->name));
	/* MENU-SCRATCH: the landing page (menus/m_omhome.rmf) offers "continue"
	   only while a level is running */
	Cvar_Set("menu_ingame", cls.state == ca_active ? "1" : "0");
	/* MENU-SCRATCH (owner t96): the landing page shows the newest save
	   ("continue" loads it outside a level); the load page shows an empty
	   state line without saves */
	if (!Q_stricmp(name, "main") || !Q_stricmp(name, "load"))
	{
		char sn[MAX_QPATH], lv[64], tm[64];
		qboolean has = M_NewestSave(sn, sizeof(sn), lv, sizeof(lv), tm, sizeof(tm));

		Cvar_Set("menu_hassave", has ? "1" : "0");
		Cvar_Set("om_lastsave", has ? sn : "");
		Cvar_Set("om_lastsave_level", has ? lv : "");
		Cvar_Set("om_lastsave_time", has ? tm : "");
		Cvar_Set("menu_nosave", has ? "0" : "1");
		Cvar_Set("menu_cancontinue", (has || cls.state == ca_active) ? "1" : "0");
	}
	/* SPEC-800: s = width / 640; OpenMerc V1 (scr_aspect 1): s = height / 480 and the
	   default frame is the centred 640 s x 480 s canvas (SCR_CanvasScale);
	   with the virtual window (MR_Virtual) s = 1, the default frame is the
	   640 x 480 canvas of the composition and the renderer scales everything
	   by MR_Scale() */
	scr->scale = MR_Virtual() ? 1.0f : SCR_CanvasScale();
	scr->ready = true;
	scr->cursor = 1;
	scr->tiptint = 0xFF000000;
	scr->tipbtint = 0xFFFFA0A0;
	Q_strlcpy(scr->tipfont, "small", sizeof(scr->tipfont));
	scr->tabdirty = true;
	scr->lasttab = -1;

	Q_strlcpy(f->name, "default", sizeof(f->name));
	f->screen = scr;
	if (MR_Virtual())
	{
		scr->hasrail = M_ScreenWantsRail(name);
		f->x = f->fx = M_CanvasX(scr->hasrail);
		f->y = f->fy = (MR_VirtualH() - 480) / 2;
		f->w = f->fw = 640;
		f->h = f->fh = 480;

		/* MENU-SCRATCH: folder screens fill the window (m_wide.c) */
		scr->wide = MWD_ScreenWide(name);

		if (scr->wide)
		{
			MWD_Layout(scr, scr->hasrail);
			MWD_DefaultFrame(scr, f);
		}
	}
	else
	{
		f->x = f->fx = SCR_CanvasX();
		f->w = f->fw = SCR_CanvasW();
		f->h = f->fh = viddef.height;
	}
	f->backfill = 0xFF000000;
	scr->frames[0] = f;
	scr->numframes = 1;
	return scr;
}

/* pointer start: the middle of the canvas */
static void
M_CentrePointer(void)
{
	mscreen_t *scr = ms.depth ? ms.stack[ms.depth - 1] : NULL;

	ms.mx = scr && scr->numframes ? (float)(scr->frames[0]->x + scr->frames[0]->w / 2)
		: (float)(MR_VirtualW() / 2);
	ms.my = (float)(MR_VirtualH() / 2);
}

static void
M_FreeScreen(mscreen_t *scr)
{
	int i, j;

	if (!scr)
	{
		return;
	}

	for (i = 0; i < scr->numframes; i++)
	{
		mframe_t *f = scr->frames[i];

		for (j = 0; j < f->numpages; j++)
		{
			MP_FreePage(f->pages[j]);
		}

		Z_Free(f);
	}

	while (scr->tints)
	{
		mtint_t *t = scr->tints;
		scr->tints = t->next;
		Z_Free(t);
	}

	while (scr->translates)
	{
		mtranslate_t *t = scr->translates;
		scr->translates = t->next;
		Z_Free(t);
	}

	if (scr->timeoutcmd)
	{
		Z_Free(scr->timeoutcmd);
	}

	if (scr->tablist)
	{
		Z_Free(scr->tablist);
	}

	Z_Free(scr);
}

/* refresh a screen (SPEC-801 section 4 "refresh") */
static void
M_RefreshScreen(mscreen_t *scr)
{
	int i, j;

	if (!scr)
	{
		return;
	}

	if (scr->config[0])
	{
		MP_ExecConfig(scr->config);
	}

	for (i = 0; i < scr->numframes; i++)
	{
		mframe_t *f = scr->frames[i];
		mpage_t *pg = M_FramePage(f);

		if (f->cpage[0])
		{
			cvar_t *v = Cvar_Get(f->cpage, "", 0);

			if (!pg || Q_stricmp(v->string, pg->name))
			{
				M_QueueCommand(va("menu %s %s", v->string, f->name), true);
				v->modified = false;
			}
		}

		if (!pg)
		{
			continue;
		}

		if (pg->reload)
		{
			char name[MAX_QPATH];

			Q_strlcpy(name, pg->name, sizeof(name));
			MP_FreePage(pg);
			f->numpages--;
			MP_ShowPage(scr, f, name, false);
		}
		else
		{
			for (j = 0; j < pg->numwidgets; j++)
			{
				if (pg->widgets[j]->cls->refresh)
				{
					pg->widgets[j]->cls->refresh(pg, pg->widgets[j]);
				}
			}
		}
	}

	M_MarkTabDirty(scr);
}

/*
 * OpenMerc V1: screens are laid out for the window size and scr_aspect of
 * their creation. vid_restart is deferred to the next frame in this engine,
 * so the stock key list "vid_restart;menu main;menu video" builds its pages
 * before the mode changes (the original restarts synchronously). When the
 * basis changed, every screen on the stack is created again under its name
 * (pages opened into frames by later commands are not restored).
 */
static void M_ClampPointer(void);

/* MENU-SCRATCH: what a rebuilt screen keeps of the old one - the scroll of
   every page still shown in a frame of the same name and the keyboard/gamepad
   position, so changing the UI scale or the rail on a scrolled page does not
   throw the player back to its top */
static void
M_KeepScreenState(mscreen_t *old, mscreen_t *scr)
{
	int i, j;

	for (i = 0; i < old->numframes; i++)
	{
		mframe_t *fo = old->frames[i];
		mpage_t *po = fo->numpages ? fo->pages[fo->numpages - 1] : NULL;

		for (j = 0; po && j < scr->numframes; j++)
		{
			mframe_t *fn = scr->frames[j];
			mpage_t *pn = fn->numpages ? fn->pages[fn->numpages - 1] : NULL;

			if (pn && !Q_stricmp(fo->name, fn->name) && !Q_stricmp(po->name, pn->name))
			{
				pn->scroll = po->scroll;
				ML_ClampScroll(pn);
				break;
			}
		}
	}

	scr->lasttab = old->lasttab;
}

static void
M_RebuildScreens(void)
{
	int i, topx = 0, topy = 0;
	qboolean keep = false;

	/* the pointer stays on the same point of the canvas */
	if (ms.depth > 0 && ms.stack[ms.depth - 1]->numframes)
	{
		topx = ms.stack[ms.depth - 1]->frames[0]->x;
		topy = ms.stack[ms.depth - 1]->frames[0]->y;
		keep = true;
	}

	for (i = 0; i < ms.depth; i++)
	{
		mscreen_t *old = ms.stack[i], *scr;
		mpage_t *pg;

		scr = M_NewScreen(old->name);
		mp_rebuilding = true;
		pg = MP_LoadPage(scr, scr->frames[0], old->name, false);
		mp_rebuilding = false;

		if (!pg || scr->invalid)
		{
			if (pg)
			{
				MP_FreePage(pg);
			}

			M_FreeScreen(scr);
			continue;
		}

		scr->frames[0]->pages[0] = pg;
		scr->frames[0]->numpages = 1;
		M_AddRail(scr);

		if (!scr->ready && !scr->controlmodel)
		{
			scr->ready = true;
		}

		scr->opentime = old->opentime;
		M_KeepScreenState(old, scr);
		ms.stack[i] = scr;
		M_FreeScreen(old);
		M_MarkTabDirty(scr);
	}

	ms.capture = NULL;
	ms.tipwidget = NULL;

	if (keep && ms.depth > 0 && ms.stack[ms.depth - 1]->numframes)
	{
		ms.mx += ms.stack[ms.depth - 1]->frames[0]->x - topx;
		ms.my += ms.stack[ms.depth - 1]->frames[0]->y - topy;
		M_ClampPointer();
		Cvar_SetValue("menu_mouse_x", ms.mx);
		Cvar_SetValue("menu_mouse_y", ms.my);
		menu_mouse_x->modified = menu_mouse_y->modified = false;
	}
	else
	{
		M_CentrePointer();
	}
}

/* ------------------------------------------------------------------ */
/* menu system (SPEC-801 section 1)                                     */

/* single-player savegame thumbnail taken when the menus open (SPEC-801 1) */
static byte m_thumb[128 * 128 * 4];
static qboolean m_hasthumb;

/* for the savegame writer (sg_host_t.Screenshot, SPEC-303 SHOT chunk) */
qboolean
M_SaveThumbnail(byte *rgba65536)
{
	if (!m_hasthumb)
	{
		return false;
	}

	memcpy(rgba65536, m_thumb, sizeof(m_thumb));
	return true;
}

static void
M_CreateSystem(void)
{
	qboolean ingame = cls.key_dest == key_game;

	ms.active = true;
	ms.forced = false;
	ms.created = ms.lastpop = Sys_Milliseconds();
	ms.cursorv = 0;
	ms.capture = NULL;
	ms.tipwidget = NULL;
	ms.lasthover = NULL;

	S_StopAllSounds();

	if (Cvar_VariableValue("maxclients") <= 1 && Com_ServerState())
	{
		if (ingame)
		{
			m_hasthumb = MR_Thumbnail(m_thumb);
		}
		else if (!m_hasthumb)
		{
			memset(m_thumb, 0, sizeof(m_thumb));
			m_hasthumb = true;
		}
	}


	ms.mx = (float)(MR_VirtualW() / 2);
	ms.my = (float)(MR_VirtualH() / 2);
	Cvar_SetValue("menu_mouse_x", ms.mx);
	Cvar_SetValue("menu_mouse_y", ms.my);
	menu_mouse_x->modified = menu_mouse_y->modified = false;

	cls.key_dest = key_menu;
	Cvar_Set("scr_showpause", "0");

	if (Cvar_VariableValue("maxclients") <= 1 && Com_ServerState())
	{
		Cvar_Set("paused", "1");
	}

	Cvar_Set("player_summary", "0/0");
	Cvar_Set("server_summary", "0");
	Cvar_Set("locked_summary", "0");
}

static void
M_CloseSystem(void)
{
	while (ms.depth > 0)
	{
		M_FreeScreen(ms.stack[--ms.depth]);
	}

	if (!ms.active)
	{
		return;
	}

	ms.active = false;
	ms.forced = false;
	ms.capture = NULL;
	ms.tipwidget = NULL;
	ms.lasthover = NULL;

	MR_ReleasePics();

	if (cls.key_dest == key_menu)
	{
		cls.key_dest = key_game;
	}

	Key_MarkAllUp();
	Cvar_Set("scr_menuoverlay", "0");
	Cvar_Set("scr_showpause", "1");
	Cvar_Set("paused", "0");
	MSP_ReleaseMenu();
	MC_ClearLobby(); /* OpenMerc (I2d): the lobby data ends with the menus */
	m_hasthumb = false; /* the savegame thumbnail is discarded */
}

/* ------------------------------------------------------------------ */
/* opening menus (SPEC-801 section 2)                                   */

static void
M_OpenMenu(const char *rawname, const char *rawframe, qboolean forced)
{
	char name[MAX_QPATH], frame[64];
	mscreen_t *top, *scr;
	mpage_t *pg;
	int i, t0 = Sys_Milliseconds();

	Q_strlcpy(name, rawname, sizeof(name));
	Q_strlwr(name);

	/* MENU-SCRATCH (owner t91u/t94u): the animated ARC main menu (globe
	   screens amain/a_main/a_amain) is gone; every way into it - the
	   intro's "menu amain", the stock home buttons - opens the rail main
	   menu (menus/main.rmf) */
	if (!strcmp(name, "amain") || !strcmp(name, "a_main") || !strcmp(name, "a_amain"))
	{
		Q_strlcpy(name, "main", sizeof(name));
	}

	/* owner t102u: no parental locks - the stock lock and password pages
	   (lock, change) are not opened */
	if (!strcmp(name, "lock") || !strcmp(name, "change"))
	{
		return;
	}

	Q_strlcpy(frame, rawframe ? rawframe : "", sizeof(frame));
	Q_strlwr(frame);

	if (frame[0] && strcmp(frame, "default"))
	{
		mframe_t *f;

		top = M_TopScreen();

		if (!top || !top->ready)
		{
			if (!top)
			{
				Com_Printf("Error : Cannot find frame \"%s\"\n", frame);
			}

			return;
		}

		f = MP_FindFrame(top, frame);

		if (!f)
		{
			Com_Printf("Error : Cannot find frame \"%s\"\n", frame);
			return;
		}

		if (!MP_ShowPage(top, f, name, false))
		{
			Com_Printf("Error : Could not create \"%s\"\n", name);
		}

		return;
	}

	if (!ms.active)
	{
		M_CreateSystem();
	}

	if (forced)
	{
		ms.forced = true;
	}

	top = M_TopScreen();

	if (top)
	{
		if (top->exitcfg[0])
		{
			M_QueueCommand(va("exec menus/%s", top->exitcfg), true);
		}

		if (top->onexitcvar[0] && Cvar_VariableValue(top->onexitcvar) != 0)
		{
			Q_strlcpy(name, top->onexitmenu, sizeof(name));
			Q_strlwr(name);
		}

		if (!top->ready)
		{
			M_PlaySound("menu/small/invalid.wav");
			return;
		}
	}

	/* menus are never duplicated on the stack */
	for (i = 0; i < ms.depth; i++)
	{
		if (!Q_stricmp(ms.stack[i]->name, name) && !ms.stack[i]->nopush)
		{
			while (ms.depth > i + 1)
			{
				M_FreeScreen(ms.stack[--ms.depth]);
			}

			ms.capture = NULL;
			ms.tipwidget = NULL;
			M_RefreshScreen(ms.stack[i]);
			return;
		}
	}

	if (ms.depth >= MENU_MAX_STACK)
	{
		M_FreeScreen(ms.stack[0]);
		memmove(ms.stack, ms.stack + 1, (MENU_MAX_STACK - 1) * sizeof(ms.stack[0]));
		ms.depth--;
	}

	scr = M_NewScreen(name);
	ms.stack[ms.depth++] = scr;
	pg = MP_LoadPage(scr, scr->frames[0], name, false);

	if (!pg || scr->invalid)
	{
		if (pg)
		{
			MP_FreePage(pg);
		}

		ms.depth--;
		M_FreeScreen(scr);

		if (ms.depth == 0)
		{
			M_CloseSystem();
		}

		return;
	}

	scr->frames[0]->pages[0] = pg;
	scr->frames[0]->numpages = 1;
	M_AddRail(scr);

	/* no model can end the wait (see m_model.c) */
	if (!scr->ready && !scr->controlmodel)
	{
		scr->ready = true;
	}

	ms.capture = NULL;
	M_MarkTabDirty(scr);
	Com_DPrintf("Overall menu creation time: %d ms\n", Sys_Milliseconds() - t0);
}

static void
M_Menu_f(void)
{
	if (Cmd_Argc() < 2)
	{
		Com_Printf("Usage: menu <name> [frame]\n");
		return;
	}

	M_OpenMenu(Cmd_Argv(1), Cmd_Argc() > 2 ? Cmd_Argv(2) : NULL, false);
}

static void
M_Intermission_f(void)
{
	if (Cmd_Argc() < 2)
	{
		Com_Printf("Usage: intermission <name> [frame]\n");
		return;
	}

	M_OpenMenu(Cmd_Argv(1), Cmd_Argc() > 2 ? Cmd_Argv(2) : NULL, true);
}

/* ------------------------------------------------------------------ */
/* leaving menus (SPEC-801 section 3)                                   */

static qboolean
M_SinglePlayerDead(void)
{
	return Cvar_VariableValue("deathmatch") == 0 && cls.state == ca_active &&
		cl.frame.playerstate.stats[1] <= 0;
}

static qboolean
M_PushableBelow(void)
{
	int i;

	for (i = 0; i < ms.depth - 1; i++)
	{
		if (!ms.stack[i]->nopush)
		{
			return true;
		}
	}

	return false;
}

static qboolean
M_PopAllowed(void)
{
	qboolean inlevel = cls.state >= ca_connected;

	if (Cvar_VariableValue("console") != 0 && !ms.forced)
	{
		return true;
	}

	if (!inlevel || ms.forced)
	{
		return M_PushableBelow();
	}

	if (M_SinglePlayerDead())
	{
		return M_PushableBelow();
	}

	return true;
}

static qboolean
M_ExitHooks(mscreen_t *top)
{
	if (top->onexitcvar[0] && Cvar_VariableValue(top->onexitcvar) != 0)
	{
		M_QueueCommand(va("menu %s", top->onexitmenu), true);
		return true;
	}

	if (top->exitcfg[0])
	{
		M_QueueCommand(va("exec menus/%s", top->exitcfg), true);
	}

	return false;
}

static void
M_PopTop(void)
{
	M_FreeScreen(ms.stack[--ms.depth]);
	ms.capture = NULL;
	ms.tipwidget = NULL;
	ms.lasthover = NULL;
	ms.lastpop = Sys_Milliseconds();
	S_StopAllSounds();
}

static void
M_PopMenu_f(void)
{
	mscreen_t *top = M_TopScreen();

	if (!top)
	{
		return;
	}

	if (M_ExitHooks(top))
	{
		return;
	}

	if (!M_PopAllowed())
	{
		M_PlaySound("menu/small/invalid.wav");
		return;
	}

	M_PopTop();

	while (ms.depth > 0 && M_TopScreen()->nopush)
	{
		if (!M_PopAllowed())
		{
			break;
		}

		M_PopTop();
	}

	if (ms.depth == 0)
	{
		M_CloseSystem();
		return;
	}

	M_RefreshScreen(M_TopScreen());
}

static void
M_KillMenu_f(void)
{
	if (!ms.depth)
	{
		return;
	}

	M_PopTop();

	if (ms.depth == 0)
	{
		M_CloseSystem();
		return;
	}

	M_RefreshScreen(M_TopScreen());
}

static void
M_MenuOff_f(void)
{
	M_CloseSystem();
}

/* MENU-SCRATCH (owner t96): "continue" of the landing page: back to the
   running level, else load the newest savegame, else the new-game page */
static void M_Return_f(void);

static void
M_Continue_f(void)
{
	char sn[MAX_QPATH], lv[64], tm[64];

	if (cls.state == ca_active)
	{
		M_Return_f();
	}
	else if (M_NewestSave(sn, sizeof(sn), lv, sizeof(lv), tm, sizeof(tm)))
	{
		Cbuf_AddText(va("load %s\n", sn));
	}
	else
	{
		Cbuf_AddText("menu game\n");
	}
}

static void
M_Return_f(void)
{
	mscreen_t *top = M_TopScreen();
	qboolean inlevel = cls.state >= ca_connected;
	qboolean close = false;

	if (!top)
	{
		return;
	}

	if (M_ExitHooks(top))
	{
		return;
	}

	if (Cvar_VariableValue("console") != 0 && !ms.forced)
	{
		close = true;
	}
	else if (inlevel && !ms.forced && !M_SinglePlayerDead())
	{
		close = true;
	}
	else if (M_SinglePlayerDead() && M_PushableBelow())
	{
		close = true;
	}

	if (close)
	{
		M_CloseSystem();
	}
	else
	{
		M_PlaySound("menu/small/invalid.wav");
	}
}

/* ------------------------------------------------------------------ */
/* other commands (SPEC-801 section 4)                                  */

static void
M_Refresh_f(void)
{
	M_RefreshScreen(M_TopScreen());
}

static void
M_ReloadAll_f(void)
{
	int i, j;

	for (i = 0; i < ms.depth; i++)
	{
		for (j = 1; j < ms.stack[i]->numframes; j++)
		{
			mpage_t *pg = M_FramePage(ms.stack[i]->frames[j]);

			if (pg)
			{
				pg->reload = true;
			}
		}
	}
}

static void
M_Select_f(void)
{
	MW_SelectCommand(Cmd_Argc());
}

static void
M_FreshGame_f(void)
{
	if (cls.state >= ca_connected)
	{
		Cbuf_AddText("menu exit"); /* without a line end (original behaviour) */
	}
	else
	{
		Cbuf_AddText("newgame\n");
	}
}

static void
M_HackTeam_f(void)
{
	if (Cmd_Argc() < 3)
	{
		return;
	}

	if (Q_stricmp(Cmd_Argv(1), Cvar_VariableString("skin")))
	{
		Cvar_Set("skin", Cmd_Argv(1));
		Cvar_Set("teamname", Cmd_Argv(2));
	}
}

/* ------------------------------------------------------------------ */
/* input (SPEC-801 section 7)                                           */

/* raw pointer movement in screen pixels (called by the input code) */
void
M_MouseMove(int dx, int dy)
{
	static int pdx, pdy;

	if (!ms.active)
	{
		return;
	}

	if (Cvar_VariableValue("m_filter"))
	{
		int fx = (dx + pdx) / 2, fy = (dy + pdy) / 2;

		pdx = dx;
		pdy = dy;
		dx = fx;
		dy = fy;
	}

	/* MENU2: the pointer moves in menu units (window pixels / MR_Scale) */
	ms.mx += dx / MR_Scale();
	ms.my += dy / MR_Scale();
}

static void
M_ClampPointer(void)
{
	if (ms.mx < -8) ms.mx = -8;
	if (ms.my < -8) ms.my = -8;
	if (ms.mx > MR_VirtualW() + 8) ms.mx = (float)(MR_VirtualW() + 8);
	if (ms.my > MR_VirtualH() + 8) ms.my = (float)(MR_VirtualH() + 8);
}

static void
M_WidgetKey(mpage_t *pg, mwidget_t *w, int key)
{
	if (!w->cls->key || w->cls->key(pg, w, key))
	{
		ML_RunKeyList(pg, w->binds, key, true);
	}
}

/* delivery of a key to the top screen */
static void
M_Deliver(int key)
{
	mscreen_t *scr = M_TopScreen();
	int i;

	if (!scr || (!scr->ready && !scr->fragile))
	{
		return;
	}

	if (key == K_MOUSE2)
	{
		scr->dragging = true;
		scr->dragx = (int)ms.mx;
		scr->dragy = (int)ms.my;
	}

	for (i = 0; i < scr->numframes && M_TopScreen() == scr; i++)
	{
		mpage_t *pg = M_FramePage(scr->frames[i]);

		if (!pg)
		{
			continue;
		}

		if (pg->focus)
		{
			M_WidgetKey(pg, pg->focus, key);
		}

		if (pg->hovered && pg->hovered != pg->focus)
		{
			M_WidgetKey(pg, pg->hovered, key);
		}

		if (ML_RunKeyList(pg, pg->binds, key, false))
		{
			scr->ready = true;
		}

		if (key == K_MOUSE1)
		{
			pg->drag = NULL;
		}
	}
}

/* frame whose content area contains the pointer */
static mframe_t *
M_FrameUnderPointer(mscreen_t *scr)
{
	int i;

	for (i = 0; i < scr->numframes; i++)
	{
		mframe_t *f = scr->frames[i];
		int x0 = f->fx + f->pad + scr->originx, y0 = f->fy + f->pad + scr->originy;
		int x1 = f->fx + f->fw - f->pad + scr->originx, y1 = f->fy + f->fh - f->pad + scr->originy;

		if (ms.mx >= x0 && ms.mx < x1 && ms.my >= y0 && ms.my < y1)
		{
			return f;
		}
	}

	return NULL;
}

static void
M_Scroll(int dir)
{
	mscreen_t *scr = M_TopScreen();
	mpage_t *pg = NULL;
	int i;

	/* MENU-SCRATCH: the innermost scrollable frame under the pointer; the
	   screen's first frame covers the whole canvas and has no scroll bar */
	for (i = scr ? scr->numframes - 1 : -1; i >= 0 && !pg; i--)
	{
		mframe_t *f = scr->frames[i];
		mpage_t *fp = M_FramePage(f);
		int x0 = f->fx + f->pad + scr->originx, y0 = f->fy + f->pad + scr->originy;
		int x1 = f->fx + f->fw - f->pad + scr->originx, y1 = f->fy + f->fh - f->pad + scr->originy;

		if (fp && fp->vbar && ms.mx >= x0 && ms.mx < x1 && ms.my >= y0 && ms.my < y1)
		{
			pg = fp;
		}
	}

	if (!pg)
	{
		return;
	}

	pg->scroll += (int)(dir * 16 * m_sensitivity_wheel->value);
	ML_ClampScroll(pg);
}

static void
M_BuildTabList(mscreen_t *scr)
{
	int i, j, n = 0, cap = 0;

	if (scr->tablist)
	{
		Z_Free(scr->tablist);
		scr->tablist = NULL;
	}

	for (i = 0; i < scr->numframes; i++)
	{
		mpage_t *pg = M_FramePage(scr->frames[i]);
		cap += pg ? pg->numwidgets : 0;
	}

	if (cap)
	{
		scr->tablist = Z_Malloc(cap * sizeof(mwidget_t *));
	}

	for (i = 0; i < scr->numframes; i++)
	{
		mpage_t *pg = M_FramePage(scr->frames[i]);

		for (j = 0; pg && j < pg->numwidgets; j++)
		{
			if (pg->widgets[j]->flags & WF_INTERACTIVE)
			{
				scr->tablist[n++] = pg->widgets[j];
			}
		}
	}

	scr->numtab = n;
	scr->tabdirty = false;
}

static qboolean
M_InView(mwidget_t *w)
{
	mpage_t *pg = w->page;
	mframe_t *f = pg->frame;
	int x, y, top, bottom;

	if (w->flags & (WF_BOLTED | WF_NOTPLACED))
	{
		return true;
	}

	ML_WidgetScreenPos(pg, w, &x, &y);
	y -= w->bpad + w->yoff;
	top = f->fy + f->pad + pg->screen->originy;
	bottom = f->fy + f->fh - f->pad + pg->screen->originy;
	return y + ML_TotalH(w) > top && y < bottom;
}

static void
M_TabNext(qboolean back)
{
	mscreen_t *scr = M_TopScreen();
	int start = -1, i, n;

	if (!scr)
	{
		return;
	}

	if (scr->tabdirty)
	{
		M_BuildTabList(scr);
	}

	n = scr->numtab;

	if (!n)
	{
		return;
	}

	for (i = 0; i < n; i++)
	{
		if (scr->tablist[i]->flags & WF_HOVER)
		{
			mpage_t *pg = scr->tablist[i]->page;

			if (pg->hovered == scr->tablist[i])
			{
				start = i;
			}
		}
	}

	if (start < 0)
	{
		start = scr->lasttab;
	}

	for (i = 1; i <= n; i++)
	{
		int k = back ? (start - i + 2 * n) % n : (start + i + n) % n;
		mwidget_t *w = scr->tablist[k];

		if (start < 0)
		{
			k = back ? (n - i) % n : (i - 1) % n;
			w = scr->tablist[k];
		}

		if (M_InView(w))
		{
			int x, y;

			ML_WidgetScreenPos(w->page, w, &x, &y);
			x -= w->bpad + w->xoff;
			y -= w->bpad + w->yoff;
			ms.mx = (float)(x + ML_TotalW(w) / 2);
			ms.my = (float)(y + ML_TotalH(w) / 2);
			ML_PageToScreen(w->page, w, &ms.mx, &ms.my);
			scr->lasttab = k;
			return;
		}
	}
}

/*
 * MENU2 gamepad (docs/DEVIATIONS.md "Gamepad menu navigation"): the next interactive widget of
 * one kind - page content (bolted 0) or the fixed tabs and buttons (bolted 1) - in TAB order;
 * a content widget outside the visible part of its scrolling page is scrolled into view
 * instead of being skipped. Returns false if the screen has no widget of that kind.
 */
static qboolean
M_PadNext(qboolean back, qboolean bolted)
{
	mscreen_t *scr = M_TopScreen();
	int start = -1, i, n;

	if (!scr)
	{
		return false;
	}

	if (scr->tabdirty)
	{
		M_BuildTabList(scr);
	}

	n = scr->numtab;

	for (i = 0; i < n; i++)
	{
		if ((scr->tablist[i]->flags & WF_HOVER) && scr->tablist[i]->page->hovered == scr->tablist[i])
		{
			start = i;
		}
	}

	if (start < 0)
	{
		start = scr->lasttab;
	}

	for (i = 1; i <= n; i++)
	{
		int k = start < 0 ? (back ? n - i : i - 1) : (back ? (start - i + 2 * n) % n : (start + i) % n);
		mwidget_t *w = scr->tablist[k];
		mpage_t *pg = w->page;
		mframe_t *f = pg->frame;
		int x, y, h, top, bottom, tries;

		if (((w->flags & WF_BOLTED) != 0) != (bolted != 0))
		{
			continue;
		}

		h = ML_TotalH(w);

		for (tries = 0; pg->vbar && !(w->flags & (WF_BOLTED | WF_NOTPLACED)) && tries < 512; tries++)
		{
			int old = pg->scroll;

			ML_WidgetScreenPos(pg, w, &x, &y);
			y -= w->bpad + w->yoff;
			top = f->fy + f->pad + scr->originy;
			bottom = f->fy + f->fh - f->pad + scr->originy;

			if (y < top)
			{
				pg->scroll -= 4;
			}
			else if (y + h > bottom && y > top)
			{
				pg->scroll += 4;
			}
			else
			{
				break;
			}

			ML_ClampScroll(pg);

			if (pg->scroll == old)
			{
				break;
			}
		}

		if (!M_InView(w))
		{
			continue;
		}

		ML_WidgetScreenPos(pg, w, &x, &y);
		x -= w->bpad + w->xoff;
		y -= w->bpad + w->yoff;
		ms.mx = (float)(x + ML_TotalW(w) / 2);
		ms.my = (float)(y + h / 2);
		ML_PageToScreen(pg, w, &ms.mx, &ms.my);
		scr->lasttab = k;
		return true;
	}

	return false;
}
void
M_Keydown(int key)
{
	mscreen_t *scr = M_TopScreen();

	if (!ms.active || !scr)
	{
		return;
	}

	/* 1. keyboard capture (key-binding row waiting for a key) */
	if (ms.capture)
	{
		mwidget_t *w = ms.capture;

		if (key == K_ESCAPE)
		{
			ms.capture = NULL;
			return;
		}

		if (Key_Char(key, M_ShiftDown()) != '`')
		{
			M_WidgetKey(w->page, w, key);
		}

		return;
	}

	/* 2. with ALT held */
	if (M_AltDown())
	{
		if (key == K_LCTRL || key == K_RCTRL)
		{
			M_Deliver(K_MOUSE1);
		}
		else if (key == K_LEFTARROW || key == K_RIGHTARROW || key == K_UPARROW || key == K_DOWNARROW)
		{
			ms.cursorv += (int)m_cursor_accel->value;

			if (ms.cursorv > (int)m_cursor_speed->value)
			{
				ms.cursorv = (int)m_cursor_speed->value;
			}

			if (key == K_LEFTARROW) ms.mx -= ms.cursorv;
			if (key == K_RIGHTARROW) ms.mx += ms.cursorv;
			if (key == K_UPARROW) ms.my -= ms.cursorv;
			if (key == K_DOWNARROW) ms.my += ms.cursorv;
			M_ClampPointer();
		}

		return;
	}

	/* MENU2: gamepad in the menus (docs/DEVIATIONS.md "Gamepad menu navigation"): d-pad up/down
	   walk the page content (scrolling it), the shoulders walk the fixed tabs and buttons, d-pad
	   left/right are the arrow keys (sliders, lists), A clicks like ENTER, B and BACK close the
	   menu like ESCAPE.
	   Key-binding rows still receive the buttons themselves (capture above). */
	if (menu_gamepad && menu_gamepad->value)
	{
		switch (key)
		{
			case K_DPAD_UP:
			case K_DPAD_DOWN:
				if (!M_PadNext(key == K_DPAD_UP, false))
				{
					M_PadNext(key == K_DPAD_UP, true);
				}

				return;

			case K_DPAD_LEFT:
				key = K_LEFTARROW;
				break;

			case K_DPAD_RIGHT:
				key = K_RIGHTARROW;
				break;

			case K_BTN_SOUTH:
				key = K_ENTER;
				break;

			case K_BTN_EAST:
			case K_BTN_BACK:
				key = K_ESCAPE;
				break;

			case K_SHOULDER_LEFT:
			case K_SHOULDER_RIGHT:
				M_PadNext(key == K_SHOULDER_LEFT, true);
				return;

			default:
				break;
		}
	}

	/* 3. */
	switch (key)
	{
		case K_ESCAPE:
			M_PopMenu_f();
			return;

		case K_MOUSE2:
			/* the popup widget of 1.07f is never shown (SPEC-802) */
			M_Deliver(key);
			return;

		case K_TAB:
			M_TabNext(M_ShiftDown());
			M_Deliver(key);
			return;

		case K_ENTER:
		case K_KP_ENTER:
			M_Deliver(K_MOUSE1);

			/* MENU-SCRATCH: when the click started a key-binding row waiting for
			   a key, the same ENTER (or gamepad A) is not that key */
			if (!ms.capture)
			{
				M_Deliver(key);
			}

			return;

		case K_MWHEELUP:
			M_Scroll(-1);
			return;

		case K_MWHEELDOWN:
			M_Scroll(1);
			return;

		case K_UPARROW:
		case K_DOWNARROW:
			if (M_ShiftDown())
			{
				M_Scroll(key == K_UPARROW ? -1 : 1);
				return;
			}

			break;

		default:
			break;
	}

	M_Deliver(key);
}

/* hover flags of one page; returns the page's hovered widget */
static mwidget_t *
M_PageHover(mscreen_t *scr, mframe_t *f, mpage_t *pg)
{
	int i, x0, y0, x1, y1;
	qboolean inside;
	mwidget_t *hovered = NULL;

	x0 = f->fx + f->pad + scr->originx;
	y0 = f->fy + f->pad + scr->originy;
	x1 = f->fx + f->fw - f->pad + scr->originx;
	y1 = f->fy + f->fh - f->pad + scr->originy;
	inside = pg->global || (ms.mx >= x0 && ms.mx < x1 && ms.my >= y0 && ms.my < y1);

	for (i = 0; i < pg->numwidgets; i++)
	{
		mwidget_t *w = pg->widgets[i];
		int x, y, tw = ML_TotalW(w), th = ML_TotalH(w);
		float mx, my;

		w->flags &= ~WF_HOVER;

		if (!inside)
		{
			continue;
		}

		/* layout rectangle: drawing offsets are not applied */
		ML_WidgetScreenPos(pg, w, &x, &y);
		x -= w->bpad + w->xoff;
		y -= w->bpad + w->yoff;

		if (w->flags & WF_BOLTED)
		{
			x += w->xoff;
			y += w->yoff;
		}

		ML_PageMouse(pg, w, &mx, &my);

		if (mx >= x && mx <= x + tw - 1 && my >= y && my <= y + th - 1)
		{
			w->flags |= WF_HOVER;

			if (w->flags & WF_INTERACTIVE)
			{
				hovered = w;
			}
		}
	}

	pg->hovered = hovered;
	return hovered;
}

/* hover and held buttons of one page (SPEC-801 section 7.2) */
static void
M_PageMouse(mscreen_t *scr, mframe_t *f, mpage_t *pg)
{
	mwidget_t *hovered;

	if (pg->drag)
	{
		if (M_KeyIsDown(K_MOUSE1))
		{
			int x, y;
			float mx, my;

			ML_WidgetScreenPos(pg, pg->drag, &x, &y);
			ML_PageMouse(pg, pg->drag, &mx, &my);

			if (pg->drag->cls->activate)
			{
				pg->drag->cls->activate(pg, pg->drag, 1, (int)mx - x, (int)my - y);
			}

			return;
		}

		pg->drag = NULL;
	}

	hovered = M_PageHover(scr, f, pg);

	if (hovered && hovered != ms.lasthover && hovered->tip)
	{
		/* a new tooltip (section 8) */
		int x, y;

		ML_WidgetScreenPos(pg, hovered, &x, &y);
		ms.tipwidget = hovered;
		Q_strlcpy(ms.tiptext, hovered->tip, sizeof(ms.tiptext));
		{
			float tx = 0, ty = (float)(y - hovered->bpad - hovered->yoff + ML_TotalH(hovered));

			ML_PageToScreen(pg, hovered, &tx, &ty);
			ms.tipx = (int)ms.mx;
			ms.tipy = (int)ty;
		}
		ms.tipstart = ms.now;
		ms.tipsound = false;
	}

	if (hovered)
	{
		ms.lasthover = hovered;
	}

	/* holding a button: activate every frame after the 0.2 s delay */
	if (hovered && ms.now - (ms.lastpop > ms.created ? ms.lastpop : ms.created) >= 200)
	{
		int button = 0;

		if (M_KeyIsDown(K_MOUSE1)) button = 1;
		else if (M_KeyIsDown(K_MOUSE2)) button = 2;
		else if (M_KeyIsDown(K_MOUSE3)) button = 3;

		if (button)
		{
			int x, y;
			float mx, my;

			ML_WidgetScreenPos(pg, hovered, &x, &y);
			ML_PageMouse(pg, hovered, &mx, &my);

			if (hovered->cls->activate)
			{
				hovered->cls->activate(pg, hovered, button, (int)mx - x, (int)my - y);
			}

			if (button == 1 && hovered->hasinc && hovered->cvar)
			{
				float v = hovered->cvar->value + hovered->inc;

				if (hovered->hasmod && hovered->mod != 0)
				{
					v = (float)fmod(v, hovered->mod);

					if (v < 0)
					{
						v += hovered->mod;
					}
				}

				Cvar_SetValue(hovered->cvar->name, v);
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* drawing (SPEC-801 sections 6, 8, 9)                                  */

static void
M_DrawTooltip(mscreen_t *scr)
{
	int D = (int)tip_duration->value, t, w, h, x, y, a;
	unsigned tt, tb;

	if (!ms.tipwidget || !scr->ready || D <= 0)
	{
		return;
	}

	t = ms.now - ms.tipstart;

	if (t > D)
	{
		ms.tipwidget = NULL;
		return;
	}

	if (t < (D >> 2))
	{
		return;
	}

	if (!ms.tipsound)
	{
		M_PlaySound("menu/small/hilite.wav");
		ms.tipsound = true;
	}

	if (t < (D >> 2) + (D >> 4) && (D >> 4) > 0)
	{
		a = 255 * (t - (D >> 2)) / (D >> 4);
	}
	else if (t > D - (D >> 4) && (D >> 4) > 0)
	{
		a = 255 * (D - t) / (D >> 4);
	}
	else
	{
		a = 255;
	}

	if (a < 0) a = 0;
	if (a > 255) a = 255;

	w = MR_StringWidth(ms.tiptext, scr->tipfont) + 4;
	h = MR_FontHeight(scr->tipfont) + 4;
	x = ms.tipx;
	y = ms.tipy;

	if (x + w > MR_VirtualW())
	{
		x = MR_VirtualW() - w;
	}

	tt = (scr->tiptint & 0x00ffffff) | ((unsigned)a << 24);
	tb = (scr->tipbtint & 0x00ffffff) | ((unsigned)a << 24);
	MR_Bevel(x - 2, y - 2, w, h, tb);
	MR_DrawString(x, y, ms.tiptext, tt, scr->tipfont, 0);
}

static void
M_DrawPointer(mscreen_t *scr, int setting)
{
	float s = scr->scale;
	int size = (int)(32 * s);

	if (setting <= 0)
	{
		return;
	}

	/* MENU-SCRATCH (owner t95u): own pointer in the wide layout */
	if (MWD_Enabled())
	{
		MR_DrawOwnCursor(ms.mx, ms.my);
		return;
	}

	MR_StretchPic((int)(ms.mx - 16 * s), (int)(ms.my - 16 * s), size, size, 0xFFFFFFFF,
			"pics/menus/cursor/cursor", 0);
}

static void
M_DrawScreen(mscreen_t *scr, qboolean input)
{
	int i, idx;
	mframe_t *under;

	if (scr->tabdirty)
	{
		M_BuildTabList(scr);
	}

	if (scr->resize)
	{
		if (input && scr->dragging)
		{
			if (M_KeyIsDown(K_MOUSE2))
			{
				scr->originx += (int)ms.mx - scr->dragx;
				scr->originy += (int)ms.my - scr->dragy;
				scr->dragx = (int)ms.mx;
				scr->dragy = (int)ms.my;
			}
			else
			{
				scr->dragging = false;
			}
		}

		/* the screen below is drawn first, without input */
		for (idx = 0; idx < ms.depth; idx++)
		{
			if (ms.stack[idx] == scr)
			{
				break;
			}
		}

		if (idx > 0 && idx < ms.depth)
		{
			M_DrawScreen(ms.stack[idx - 1], false);

			/* MENU2: darken the screen below, so its texts do not run on around the dialog */
			if (menu_dialogdim && menu_dialogdim->value > 0)
			{
				float d = menu_dialogdim->value > 1 ? 1 : menu_dialogdim->value;

				MR_ResetClip();
				MR_Fill(0, 0, MR_VirtualW(), MR_VirtualH(), (unsigned)(d * 255.0f) << 24);
			}
		}
	}

	under = M_FrameUnderPointer(scr);

	if (input && under)
	{
		mpage_t *pg = M_FramePage(under);
		scr->cursor = pg ? (pg->cursor < 0 ? 1 : pg->cursor) : 0;
	}

	for (i = 0; i < scr->numframes; i++)
	{
		mframe_t *f = scr->frames[i];
		mpage_t *pg = M_FramePage(f);

		if (input && pg)
		{
			M_PageMouse(scr, f, pg);
		}

		/* MENU-SCRATCH: a dialog's border frame fills only its rim with the
		   translucent "smokey"; over the openMERC wide screens the landing
		   page's buttons showed through its text, so its inside is filled with
		   the frame's backfill colour made opaque (stock "smokey": black; the own
		   dialogs: the panel colour) */
		if (scr->resize && MWD_Enabled() && f->border && (f->backfill >> 24))
		{
			MR_Fill(f->x + scr->originx, f->y + scr->originy, f->w, f->h, f->backfill | 0xFF000000u);
		}

		ML_DrawFrame(scr, f);
	}

	if (!input)
	{
		return;
	}

	M_DrawPointer(scr, scr->cursor);
	M_DrawTooltip(scr);

	if (scr->hastimeout && (ms.now - scr->opentime) >= (int)(scr->timeout * 1000.0f))
	{
		scr->hastimeout = false;

		if (scr->timeoutcmd)
		{
			Cbuf_AddText(scr->timeoutcmd);
			Cbuf_AddText("\n");
		}
	}
}

void
M_Draw(void)
{
	mscreen_t *scr = M_TopScreen();
	qboolean drawn = false;

	if (cls.key_dest == key_menu && ms.active && scr)
	{
		if (m_layoutw != viddef.width || m_layouth != viddef.height ||
			m_layoutaspect != (SCR_AspectCorrect() ? 1 : 0) || m_layoutscale != MR_Scale() ||
			m_layoutrail != (MR_RailShown() ? 1 : 0) || m_layoutwide != (MWD_Enabled() ? 1 : 0))
		{
			M_RebuildScreens();
			scr = M_TopScreen();

			if (!scr)
			{
				return;
			}
		}

		ms.drawing = true;
		ms.now = Sys_Milliseconds();
		ms.framecount++;

		if (menu_mouse_x->modified || menu_mouse_y->modified)
		{
			ms.mx = menu_mouse_x->value;
			ms.my = menu_mouse_y->value;
		}

		M_ClampPointer();

		MR_BeginFrame();
		M_DrawScreen(scr, true);
		MR_EndFrame();
		MT_FrameEnd();
		drawn = true;

		if ((int)menu_mouse_x->value != (int)ms.mx)
		{
			Cvar_SetValue("menu_mouse_x", (float)(int)ms.mx);
		}

		if ((int)menu_mouse_y->value != (int)ms.my)
		{
			Cvar_SetValue("menu_mouse_y", (float)(int)ms.my);
		}

		menu_mouse_x->modified = menu_mouse_y->modified = false;
		ms.drawing = false;
	}

	MS_ServerFrame();
	M_RunQueue();
	MT_Frame(drawn);
}

/* ------------------------------------------------------------------ */
/* entry points used by the rest of the client                          */

void
M_Menu_Main_f(void)
{
	/* SPEC-801 section 10: ESC in game or console */
	Cbuf_AddText("menu main\n");
}

void
M_ForceMenuOff(void)
{
	M_CloseSystem();
}

/* Yamagi Quake II hooks that other client code still calls: the renderer's
   video-menu init (refimport), the gyro calibration notice of the input code
   and the map-list cache of the filesystem. The SoF menus are .rmf pages, so
   there is nothing to do. */
void
VID_MenuInit(void)
{
}

void
CalibrationFinishedCallback(void)
{
}

void
CleanCachedMapsList(void)
{
}

/* test hook: deliver a key to the menus by name (OpenMerc extension,
   used by the console-driven menu tests) */
static void
M_TestKey_f(void)
{
	int k, a = 1, i;
	int mods[3] = {0, 0, 0};
	qboolean held[3];
	static const int modkeys[3] = {K_LSHIFT, K_LALT, K_LCTRL};
	mscreen_t *scr;

	/* om_menukey [+shift] [+alt] [+ctrl] <keyname> [x y]; keyname "none"
	   (MENU2) only moves the pointer, for hover tests */
	for (; a < Cmd_Argc() && Cmd_Argv(a)[0] == '+'; a++)
	{
		if (!Q_stricmp(Cmd_Argv(a), "+shift")) mods[0] = 1;
		if (!Q_stricmp(Cmd_Argv(a), "+alt")) mods[1] = 1;
		if (!Q_stricmp(Cmd_Argv(a), "+ctrl")) mods[2] = 1;
	}

	if (a >= Cmd_Argc())
	{
		Com_Printf("usage: om_menukey [+shift] [+alt] [+ctrl] <keyname> [x y]\n");
		return;
	}

	k = Q_stricmp(Cmd_Argv(a), "none") ? Key_StringToKeynum(Cmd_Argv(a)) : -2;

	if (k == -1)
	{
		Com_Printf("\"%s\" isn't a valid key\n", Cmd_Argv(a));
		return;
	}

	scr = M_TopScreen();

	if (Cmd_Argc() >= a + 3)
	{
		ms.mx = (float)atof(Cmd_Argv(a + 1));
		ms.my = (float)atof(Cmd_Argv(a + 2));
		Cvar_SetValue("menu_mouse_x", ms.mx);
		Cvar_SetValue("menu_mouse_y", ms.my);
		menu_mouse_x->modified = menu_mouse_y->modified = false;

		/* hover follows the pointer at once (normally on the next frame) */
		for (i = 0; scr && i < scr->numframes; i++)
		{
			mpage_t *pg = M_FramePage(scr->frames[i]);

			if (pg)
			{
				M_PageHover(scr, scr->frames[i], pg);
			}
		}
	}

	if (k == -2)
	{
		return;
	}

	for (i = 0; i < 3; i++)
	{
		held[i] = keydown[modkeys[i]];

		if (mods[i])
		{
			keydown[modkeys[i]] = true;
		}
	}

	M_Keydown(k);

	for (i = 0; i < 3; i++)
	{
		keydown[modkeys[i]] = held[i];
	}
}

void
M_Init(void)
{
	menu_mouse_x = Cvar_Get("menu_mouse_x", "0", 0);
	menu_mouse_y = Cvar_Get("menu_mouse_y", "0", 0);
	m_cursor_speed = Cvar_Get("m_cursor_speed", "8", CVAR_ARCHIVE);
	m_cursor_accel = Cvar_Get("m_cursor_accel", "2", CVAR_ARCHIVE);
	m_sensitivity_wheel = Cvar_Get("m_sensitivity_wheel", "1", 0);
	tip_duration = Cvar_Get("tip_duration", "2500", CVAR_ARCHIVE);
	cursor_blink_speed = Cvar_Get("cursor_blink_speed", "500", CVAR_ARCHIVE);
	ghl_no_texture_stack = Cvar_Get("ghl_no_texture_stack", "0", CVAR_ARCHIVE);
	scr_menuoverlay = Cvar_Get("scr_menuoverlay", "0", 0);
	menu_dialogdim = Cvar_Get("menu_dialogdim", "0.6", CVAR_ARCHIVE);
	menu_gamepad = Cvar_Get("menu_gamepad", "1", CVAR_ARCHIVE);
	m_doublespeed = Cvar_Get("m_doublespeed", "200", 0);
	Cvar_Get("menu_dc_reason", "", 0);
	Cvar_Get("menu_won_error", "", 0);
	Cvar_Get("menu_loading", "0", 0);

	Cmd_AddCommand("menu", M_Menu_f);
	Cmd_AddCommand("intermission", M_Intermission_f);
	Cmd_AddCommand("popmenu", M_PopMenu_f);
	Cmd_AddCommand("killmenu", M_KillMenu_f);
	Cmd_AddCommand("menuoff", M_MenuOff_f);
	Cmd_AddCommand("return", M_Return_f);
	Cmd_AddCommand("om_continue", M_Continue_f);
	Cmd_AddCommand("refresh", M_Refresh_f);
	Cmd_AddCommand("reloadall", M_ReloadAll_f);
	Cmd_AddCommand("select", M_Select_f);
	Cmd_AddCommand("freshgame", M_FreshGame_f);
	Cmd_AddCommand("hackteam", M_HackTeam_f);
	Cmd_AddCommand("om_menukey", M_TestKey_f);
	/* userweapons, useritems, userammo, usermisc: engine core (I1a, sof_engine.c) */

	MS_Init();
	MSP_Init();
	MT_Init();

	/* MENU-SCRATCH: the version the menus show (menus/version.rmf); the
	   "version" cvar stays the SoF protocol string of the serverinfo */
	Cvar_Get("om_version", OM_VERSION_STRING, CVAR_NOSET);
	Cvar_ForceSet("om_version", OM_VERSION_STRING);
	Cvar_Get("om_build", OM_BUILD, CVAR_NOSET);
	Cvar_ForceSet("om_build", OM_BUILD);
}

void
M_Free(void)
{
	M_CloseSystem();
	MR_Shutdown();

	while (mq_count > 0)
	{
		Z_Free(mq_cmds[--mq_count]);
	}
}
