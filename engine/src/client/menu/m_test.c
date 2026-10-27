/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc: menu runtime self-tests (console commands, OpenMerc only).
 *
 *   om_rmftest        parses every menus/**.rmf of the search path with the
 *                     runtime parser (no side effects) and prints counts:
 *                     documents, fragments, widgets, parse messages by kind.
 *   om_rmfopen <name> opens a menu like "menu", then prints the frames and
 *                     widgets of the new screen (positions, sizes) - used to
 *                     compare layouts with the specs' test vectors.
 *
 * Implements: SPEC-130 (validation of every stock file), SPEC-800 (section 4
 *             messages), SPEC-801 (test commands are an OpenMerc extension)
 */

#include "header/menu.h"
#include "header/m_draw.h"
#include "../../common/header/sof/engine.h"

extern int mp_errorcount;
extern void (*mp_errorhook)(const char *tag, const char *msg);

#define MT_KINDS 64

static struct
{
	char text[64];
	int count;
	char example[96];
} mt_kinds[MT_KINDS];
static int mt_numkinds;
static char mt_file[MAX_QPATH];
static qboolean mt_verbose;

static void
MT_Hook(const char *tag, const char *msg)
{
	char kind[64];
	int i, n = 0;
	const char *s;

	if (mt_verbose)
	{
		Com_Printf("  %s: <%s> %s\n", mt_file, tag, msg);
	}

	/* message kind = text up to the first quote or digit-free name part */
	for (s = msg; *s && *s != '\'' && *s != '"' && n < 63; s++)
	{
		kind[n++] = *s;
	}

	kind[n] = 0;

	if (!strncmp(kind, "Unable to include ", 18) || !strncmp(kind, "Unable to find ", 15))
	{
		/* keep the argument: these are per-file facts */
		Q_strlcpy(kind, msg, sizeof(kind));
	}

	for (i = 0; i < mt_numkinds; i++)
	{
		if (!strcmp(mt_kinds[i].text, kind))
		{
			break;
		}
	}

	if (i == mt_numkinds)
	{
		if (mt_numkinds == MT_KINDS)
		{
			return;
		}

		Q_strlcpy(mt_kinds[i].text, kind, sizeof(mt_kinds[i].text));
		Com_sprintf(mt_kinds[i].example, sizeof(mt_kinds[i].example), "%s: %s", mt_file, msg);
		mt_numkinds++;
	}

	mt_kinds[i].count++;
}

static mscreen_t *
MT_Screen(void)
{
	mscreen_t *scr = Z_Malloc(sizeof(*scr));
	mframe_t *f = Z_Malloc(sizeof(*f));

	Q_strlcpy(scr->name, "om_rmftest", sizeof(scr->name));
	scr->scale = 1.0f;
	scr->ready = true;
	Q_strlcpy(f->name, "default", sizeof(f->name));
	f->screen = scr;
	f->w = f->fw = 640;
	f->h = f->fh = 480;
	scr->frames[0] = f;
	scr->numframes = 1;
	return scr;
}

static void
MT_FreeScreen(mscreen_t *scr)
{
	int i;

	for (i = 0; i < scr->numframes; i++)
	{
		Z_Free(scr->frames[i]);
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

	Z_Free(scr);
}

static int
MT_SortNames(const void *a, const void *b)
{
	return Q_stricmp(*(char *const *)a, *(char *const *)b);
}

static void
MT_RmfTest_f(void)
{
	strlist_t files, more, packs;
	int i, docs = 0, frags = 0, widgets = 0, failed = 0, errfiles = 0;

	mt_numkinds = 0;
	memset(mt_kinds, 0, sizeof(mt_kinds));
	mt_verbose = Cmd_Argc() > 1 && !Q_stricmp(Cmd_Argv(1), "v");

	/* every string package, so that references of fragments resolve */
	packs = FS_ListFiles2("strip/*.sp", 0, SFF_SUBDIR);

	for (i = 0; i < packs.num; i++)
	{
		char name[MAX_QPATH], *dot;
		const char *base = strrchr(packs.data[i], '/');

		Q_strlcpy(name, base ? base + 1 : packs.data[i], sizeof(name));
		dot = strrchr(name, '.');

		if (dot)
		{
			*dot = 0;
		}

		MSP_Register(name);
	}

	StrList_Free(&packs);

	files = FS_ListFiles2("menus/*.rmf", 0, SFF_SUBDIR);
	more = FS_ListFiles2("menus/*/*.rmf", 0, SFF_SUBDIR);

	for (i = 0; i < more.num; i++)
	{
		if (!StrList_Contains(&files, more.data[i]))
		{
			StrList_Append(&files, more.data[i]);
		}
	}

	StrList_Free(&more);
	qsort(files.data, files.num, sizeof(char *), MT_SortNames);
	mp_errorhook = MT_Hook;

	for (i = 0; i < files.num; i++)
	{
		const char *path = files.data[i];
		char name[MAX_QPATH];
		char *text;
		int len, before = mp_errorcount;
		mscreen_t *scr;

		if (Q_strncasecmp(path, "menus/", 6) || !MP_ReadFile(path, &text, &len))
		{
			continue;
		}

		Q_strlcpy(name, path + 6, sizeof(name));
		name[strlen(name) - 4] = 0;
		Q_strlcpy(mt_file, path, sizeof(mt_file));
		scr = MT_Screen();

		if (Q_strcasestr(text, "<stm") && Q_strcasestr(text, "</stm"))
		{
			mpage_t *pg = MP_LoadPage(scr, scr->frames[0], name, true);

			docs++;

			if (pg)
			{
				widgets += pg->numwidgets;
				MP_FreePage(pg);
			}
			else
			{
				failed++;
			}
		}
		else
		{
			/* fragment: parsed as a page body */
			mparse_t p;
			mpage_t *pg = Z_Malloc(sizeof(*pg));

			frags++;
			memset(&p, 0, sizeof(p));
			MP_InitPage(pg, scr, scr->frames[0], name);
			pg->dry = true;
			p.page = pg;
			p.screen = scr;
			p.dry = true;
			p.fragment = true;
			p.line = 1;
			p.buf = malloc(len + 1);
			memcpy(p.buf, text, len + 1);
			p.len = p.cap = len;
			p.cap = len + 1;
			MP_ParseText(&p);
			free(p.buf);
			widgets += pg->numwidgets;
			MP_FreePage(pg);
		}

		if (mp_errorcount != before)
		{
			errfiles++;
		}

		MT_FreeScreen(scr);
		free(text);
	}

	mp_errorhook = NULL;

	Com_Printf("om_rmftest: %d files, %d documents, %d fragments, %d documents not created, "
			"%d widgets, %d files with messages\n", files.num, docs, frags, failed, widgets, errfiles);

	for (i = 0; i < mt_numkinds; i++)
	{
		Com_Printf("  %5d  %-40s  e.g. %s\n", mt_kinds[i].count, mt_kinds[i].text, mt_kinds[i].example);
	}

	StrList_Free(&files);
	MSP_ReleaseMenu();
}

static const char *
MT_ClassName(const mwidget_t *w)
{
	return w->cls ? w->cls->name : "?";
}

static void
MT_Dump_f(void)
{
	mscreen_t *scr = M_TopScreen();
	int i, j;

	if (!scr)
	{
		Com_Printf("om_menudump: no menu\n");
		return;
	}

	Com_Printf("screen %s scale %.4f ready %d nopush %d resize %d depth %d keydest %d state %d "
			"disable_screen %g frames %d pointer %d %d\n", scr->name, scr->scale, scr->ready, scr->nopush,
			scr->resize, ms.depth, cls.key_dest, cls.state, cls.disable_screen, ms.framecount, (int)ms.mx,
			(int)ms.my);

	for (i = 0; i < scr->numframes; i++)
	{
		mframe_t *f = scr->frames[i];
		mpage_t *pg = f->numpages ? f->pages[f->numpages - 1] : NULL;

		Com_Printf(" frame %s rect %d %d %d %d free %d %d %d %d pad %d page %s\n", f->name, f->x, f->y, f->w,
				f->h, f->fx, f->fy, f->fw, f->fh, f->pad, pg ? pg->name : "-");

		if (pg)
		{
			MM_Dump(pg);
		}

		for (j = 0; pg && j < pg->numwidgets; j++)
		{
			mwidget_t *w = pg->widgets[j];

			Com_Printf("  %-9s %4d %4d %4d %4d%s%s %s\n", MT_ClassName(w), w->x, w->y, ML_TotalW(w), ML_TotalH(w),
					(w->flags & WF_INTERACTIVE) ? " i" : "", (w->flags & WF_BOLTED) ? " b" : "",
					w->text ? w->text : "");
		}
	}
}

/* om_menushot <name>: capture the next menu frame as <user>/menushots/<name>.tga */
static char mt_shot[MAX_QPATH];
static char mt_shotscreen[MAX_QPATH];

static void
MT_Shot_f(void)
{
	if (Cmd_Argc() < 2)
	{
		Com_Printf("usage: om_menushot <name>\n");
		return;
	}

	Q_strlcpy(mt_shot, Cmd_Argv(1), sizeof(mt_shot));
	Q_strlcpy(mt_shotscreen, M_TopScreen() ? M_TopScreen()->name : "", sizeof(mt_shotscreen));
}

void
MT_FrameEnd(void)
{
	char path[MAX_OSPATH];

	if (!mt_shot[0])
	{
		return;
	}

	if (!M_TopScreen() || Q_stricmp(M_TopScreen()->name, mt_shotscreen))
	{
		Com_Printf("om_menushot %s: screen changed before a frame was drawn (%s)\n", mt_shot,
				M_TopScreen() ? M_TopScreen()->name : "none");
		mt_shot[0] = 0;
		return;
	}

	Com_sprintf(path, sizeof(path), "%s/menushots", FS_Userdir());
	FS_CreatePath(va("%s/", path));
	Com_sprintf(path, sizeof(path), "%s/menushots/%s.tga", FS_Userdir(), mt_shot);
	Com_Printf("%s %s\n", MR_Capture(path) ? "Wrote" : "Error writing", path);
	mt_shot[0] = 0;
}

/* om_menutour <frames> <quit|stay> <name>...: opens each menu (after menuoff),
   waits until <frames> frames of it were drawn, captures it as
   menushots/<name>.tga, goes on; a menu that draws nothing for 5 s is skipped */
#define MT_TOUR 96

static char mt_tour[MT_TOUR][MAX_QPATH];
static int mt_tourn, mt_touri = -1, mt_tourframes, mt_tourcount, mt_tourstart;
static qboolean mt_tourquit;

static void
MT_TourOpen(void)
{
	mt_tourcount = 0;
	mt_tourstart = Sys_Milliseconds();
	Cbuf_AddText(va("menuoff\nmenu %s\n", mt_tour[mt_touri]));
}

static void
MT_Tour_f(void)
{
	int i;

	if (Cmd_Argc() < 4)
	{
		Com_Printf("usage: om_menutour <frames> <quit|stay> <menu>...\n");
		return;
	}

	mt_tourframes = atoi(Cmd_Argv(1));
	mt_tourquit = !Q_stricmp(Cmd_Argv(2), "quit");
	mt_tourn = 0;

	for (i = 3; i < Cmd_Argc() && mt_tourn < MT_TOUR; i++)
	{
		Q_strlcpy(mt_tour[mt_tourn++], Cmd_Argv(i), MAX_QPATH);
	}

	mt_touri = 0;
	MT_TourOpen();
}

static void
MT_TourNext(void)
{
	if (++mt_touri < mt_tourn)
	{
		MT_TourOpen();
		return;
	}

	mt_touri = -1;
	Com_Printf("om_menutour: done\n");

	if (mt_tourquit)
	{
		Cbuf_AddText("menuoff\nquit\n");
	}
}

/* om_after <frames> <text>: console text run after <frames> client frames
   (the console's "wait" is not tied to drawn frames in OpenMerc) */
#define MT_AFTER 128

static struct
{
	int frames;
	char text[1024];
} mt_after[MT_AFTER];

static void
MT_After_f(void)
{
	int i;

	if (Cmd_Argc() < 3)
	{
		Com_Printf("usage: om_after <frames> <text>\n");
		return;
	}

	for (i = 0; i < MT_AFTER; i++)
	{
		if (!mt_after[i].text[0])
		{
			mt_after[i].frames = atoi(Cmd_Argv(1));
			Q_strlcpy(mt_after[i].text, Cmd_Args() + strlen(Cmd_Argv(1)) + 1, sizeof(mt_after[i].text));

			/* drop the quotes of a single quoted argument */
			if (Cmd_Argc() == 3)
			{
				Q_strlcpy(mt_after[i].text, Cmd_Argv(2), sizeof(mt_after[i].text));
			}

			return;
		}
	}

	Com_Printf("om_after: too many pending\n");
}

static void
MT_AfterFrame(void)
{
	int i;

	for (i = 0; i < MT_AFTER; i++)
	{
		if (mt_after[i].text[0] && --mt_after[i].frames <= 0)
		{
			Cbuf_AddText(mt_after[i].text);
			Cbuf_AddText("\n");
			mt_after[i].text[0] = 0;
		}
	}
}

void
MT_Frame(qboolean drawn)
{
	mscreen_t *top = M_TopScreen();

	MT_AfterFrame();

	if (mt_touri < 0)
	{
		return;
	}

	if (drawn && top && !Q_stricmp(top->name, mt_tour[mt_touri]))
	{
		if (++mt_tourcount >= mt_tourframes)
		{
			char path[MAX_OSPATH];

			Com_sprintf(path, sizeof(path), "%s/menushots", FS_Userdir());
			FS_CreatePath(va("%s/", path));
			Com_sprintf(path, sizeof(path), "%s/menushots/%s.tga", FS_Userdir(), mt_tour[mt_touri]);
			Com_Printf("om_menutour: %s after %d frames, %d ms: %s\n", mt_tour[mt_touri], mt_tourcount,
					Sys_Milliseconds() - mt_tourstart, MR_Capture(path) ? "captured" : "capture failed");
			MT_Dump_f();
			MT_TourNext();
		}

		return;
	}

	if (Sys_Milliseconds() - mt_tourstart > 5000)
	{
		Com_Printf("om_menutour: %s skipped (top screen %s)\n", mt_tour[mt_touri], top ? top->name : "none");
		MT_TourNext();
	}
}

/* om_keyevent <keyname>: one press and release of a key through the
   normal key handler (as if typed), e.g. ESCAPE during a cinematic */
static void
MT_KeyEvent_f(void)
{
	int key;

	if (Cmd_Argc() != 2)
	{
		Com_Printf("usage: om_keyevent <keyname>\n");
		return;
	}

	key = Key_StringToKeynum(Cmd_Argv(1));

	if (key < 0)
	{
		Com_Printf("om_keyevent: unknown key %s\n", Cmd_Argv(1));
		return;
	}

	Key_Event(key, true, false);
	Key_Event(key, false, false);
}

/* MENU-SCRATCH: om_menuprobe <tag> - the widget under the pointer of the top
   screen (after keyboard or gamepad navigation) with its cvar and value, for
   the scripted control tests (tests/MENU2/scratch_controls.cfg) */
static void
MT_Probe_f(void)
{
	mscreen_t *scr = M_TopScreen();
	int i;

	Com_Printf("PROBESTATE key_dest %i cls.state %i active %i overlay %s paused %s cinfreeze %i depth %i\n", (int)cls.key_dest,
		(int)cls.state, (int)ms.active, Cvar_VariableString("scr_menuoverlay"), Cvar_VariableString("paused"),
		(int)CL_CinematicFreezeActive(), ms.depth);

	if (!scr)
	{
		Com_Printf("PROBE %s none\n", Cmd_Argv(1));
		return;
	}

	for (i = scr->numframes - 1; i >= 0; i--)
	{
		mframe_t *f = scr->frames[i];
		mpage_t *pg = f->numpages ? f->pages[f->numpages - 1] : NULL;
		mwidget_t *w = pg ? pg->hovered : NULL;

		if (w)
		{
			Com_Printf("PROBE %s %s/%s %s cvar %s = \"%s\" text \"%s\"\n", Cmd_Argv(1), scr->name, pg->name,
					MT_ClassName(w), w->cvar ? w->cvar->name : "-", w->cvar ? w->cvar->string : "",
					w->text ? w->text : "");
			return;
		}
	}

	Com_Printf("PROBE %s %s nothing hovered\n", Cmd_Argv(1), scr->name);
}

void
MT_Init(void)
{
	Cmd_AddCommand("om_menutour", MT_Tour_f);
	Cmd_AddCommand("om_after", MT_After_f);
	Cmd_AddCommand("om_menushot", MT_Shot_f);
	Cmd_AddCommand("om_rmftest", MT_RmfTest_f);
	Cmd_AddCommand("om_menudump", MT_Dump_f);
	Cmd_AddCommand("om_keyevent", MT_KeyEvent_f);
	Cmd_AddCommand("om_menuprobe", MT_Probe_f);
}
