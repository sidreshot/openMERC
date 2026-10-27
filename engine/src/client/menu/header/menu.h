/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc: Soldier of Fortune menu runtime (.rmf pages) - internal
 * declarations shared by the files of engine/src/client/menu/.
 *
 * Implements: SPEC-130, SPEC-800, SPEC-801, SPEC-802, SPEC-803
 */

#ifndef OM_MENU_H
#define OM_MENU_H

#include "../../header/client.h"

/* RGBA colour, red in the lowest byte (0xAABBGGRR in .rmf text) */
typedef unsigned int mcolor_t;

#define MENU_MAX_TOKEN 4096
#define MENU_MAX_STACK 32
#define MENU_MAX_TABS 32
#define MENU_FONT_LEN 16

typedef struct mpage_s mpage_t;
typedef struct mframe_s mframe_t;
typedef struct mscreen_s mscreen_t;
typedef struct mwidget_s mwidget_t;
typedef struct mparse_s mparse_t;
typedef struct mmodel_s mmodel_t;

/* alignment (SPEC-800 section 6) */
enum { MA_NORMAL, MA_CENTER, MA_LEFT, MA_RIGHT };

/* MENU-SCRATCH: openMERC wide layout (m_wide.c) */
typedef struct { int x, y, w, h; } mrect_t;
typedef struct
{
	mrect_t nav, col, panel, title, foot, tick, main; /* tick: the ticker lane above the button row */
	float sx; /* main.w / 560 (the stock main area) */
} mwgeom_t;

/* widget flags */
#define WF_INTERACTIVE 0x0001
#define WF_NOSHADE     0x0002
#define WF_NOSCALE     0x0004
#define WF_TAB         0x0008
#define WF_REGULAR     0x0010
#define WF_HOVER       0x0020 /* pointer over the widget this frame */
#define WF_BOLTED      0x0040
#define WF_VARIABLE    0x0080 /* page accepts no widget after this one */
#define WF_GLOBAL      0x0100 /* input: keyboard focus of the page */
#define WF_HIDDEN      0x0200 /* input: password field */
#define WF_INVISIBLE   0x0400
#define WF_NOTPLACED   0x0800 /* vbar, bghoul, translate: not in the flow */
#define WF_HASBORDER   0x1000
#define WF_SAWALIGN    0x2000 /* explicit align option */
#define WF_BBOLT       0x4000 /* bolted to the first background model */
#define WF_TEXTLIKE    0x8000 /* bare text / <text> without "regular": starts at pen y */
#define WF_HIDEBORDER  0x10000 /* set by a draw function: no border this frame */
#define WF_DISABLED    0x20000
#define WF_NOSCROLL    0x40000 /* vbar: does not scroll with the page */

/* a key binding of a widget or a page (SPEC-800 section 5.4) */
typedef struct mbind_s
{
	struct mbind_s *next;
	int key;            /* key number, -1 = any */
	char *cmd0;         /* the command; ckey: command when the cvar is 0 */
	char *cmd1;         /* ckey: command when the cvar is not 0, else NULL */
	char cvar[64];      /* ckey cvar name */
	qboolean ckey;
} mbind_t;

/* widget class: per-tag behaviour (SPEC-802/803) */
typedef struct mwclass_s
{
	const char *name;   /* tag name used in "Unrecognised <name> keyword" */
	/* own option; return false when the keyword is not an own option */
	qboolean (*option)(mparse_t *p, mwidget_t *w, const char *kw);
	/* after all options of the tag: false = the widget is not created */
	qboolean (*finish)(mparse_t *p, mwidget_t *w);
	/* end of the page parse: false = the widget is removed */
	qboolean (*endpage)(mpage_t *pg, mwidget_t *w);
	void (*refresh)(mpage_t *pg, mwidget_t *w);
	void (*update)(mpage_t *pg, mwidget_t *w);
	/* x, y: screen position of the inner area */
	void (*draw)(mpage_t *pg, mwidget_t *w, int x, int y);
	/* key handling; return true to run the key list afterwards */
	qboolean (*key)(mpage_t *pg, mwidget_t *w, int key);
	/* mouse button held (SPEC-801 7.2); mx, my = pointer relative to the inner area */
	void (*activate)(mpage_t *pg, mwidget_t *w, int button, int mx, int my);
	void (*freedata)(mwidget_t *w);
} mwclass_t;

struct mwidget_s
{
	const mwclass_t *cls;
	mpage_t *page;
	int x, y;           /* page position of the total rectangle */
	int w, h;           /* inner size */
	int minh;
	float scale;
	int bpad, bthick;
	mcolor_t bcolor;
	int xoff, yoff;
	int nudgex, nudgey;	/* MENU-SCRATCH: 'nudge' option, applied after bolt alignment */
	int split;		/* MENU-SCRATCH: 'split' option: half width of a two-column label/value row */
	mcolor_t tint, atint, btint, ctint, dtint;
	char font[MENU_FONT_LEN];
	int align;
	int flags;
	mbind_t *binds;
	char *tip;
	float inc, mod;
	qboolean hasinc, hasmod;
	cvar_t *cvar;
	int cond;           /* -1 no condition, 0 false, 1 true */
	char *text;         /* positional text (text, ticker, list labels ...) */
	/* bolts */
	mmodel_t *boltmodel;
	int bolt;
	int boltx, bolty;   /* screen point of the bolt, updated every frame */
	qboolean boltfacing;
	void *data;         /* class specific */
	int omslot;         /* MENU-SCRATCH: slot of the wide layout (m_wide.c), 0 = none */
	int omrect[4];      /* its rectangle this frame */
};

/* page (SPEC-800): one parsed document in a frame */
struct mpage_s
{
	char name[MAX_QPATH];
	mframe_t *frame;
	mscreen_t *screen;

	mwidget_t **widgets;
	int numwidgets, maxwidgets;
	mbind_t *binds;

	/* text appearance */
	char font[MENU_FONT_LEN];
	mcolor_t ftint, fatint;

	/* flow layout */
	int penx, peny, linetop, linebottom, anchor, contenth;
	int align;
	int tabs[MENU_MAX_TABS];
	int numtabs;
	int vbarw;
	mwidget_t *vbar;
	qboolean variable;

	/* <defaults border> */
	qboolean dborder;
	int dbpad, dbthick;
	mcolor_t dbcolor;

	/* <backdrop> */
	qboolean backdrop;
	mcolor_t bgcolor;
	char bgpic[MAX_QPATH];
	int bgmode;         /* 0 none, 1 tile, 2 stretch, 3 center, 4 left, 5 right */
	qboolean bgflat;    /* MENU-SCRATCH: <backdrop omflat>, the own background */

	/* Ghoul models */
	mmodel_t *models[64];
	int nummodels;
	mmodel_t *curmodel;  /* last <ghoul>/<bghoul> */
	mmodel_t *firstbg;   /* first <bghoul> */

	int scroll;
	qboolean autoscroll;
	float autorate;
	int cursor;          /* -1 not set, 0 hidden, 1 shown */
	qboolean global;

	mwidget_t *focus;    /* input with "global" */
	mwidget_t *hovered;
	mwidget_t *drag;
	int popups, chats;
	qboolean reload;     /* marked by reloadall */
	qboolean failed;     /* a widget cvar could not be created */
	qboolean dry;        /* parse test */
	int clip[4];         /* clip rectangle of the current draw */

	/* MENU-SCRATCH page zoom (m_parse.c MP_PageZoom): a sparse page of a wide
	   screen is laid out in zframe (its frame divided by zoom) and drawn
	   magnified; zdx/zdy translate page units for MR_ZoomBegin */
	float zoom;
	int zdx, zdy;
	int zcontent;        /* content height in page units, pads included */
	mframe_t *zframe;
};

#define MENU_MAX_HISTORY 16

/* frame (SPEC-800 section 8) */
struct mframe_s
{
	char name[64];
	mscreen_t *screen;
	int x, y, w, h;          /* rectangle at creation */
	int fx, fy, fw, fh;      /* free rectangle */
	int pad, thick;
	mcolor_t bcolor, backfill;
	qboolean border;
	char cpage[64];          /* cvar holding the page name */
	qboolean rail;           /* MENU-SCRATCH: the navigation rail (never extended to the window) */
	mpage_t *pages[MENU_MAX_HISTORY];
	int numpages;
};

typedef struct mtint_s
{
	struct mtint_s *next;
	char name[64];
	mcolor_t value;
} mtint_t;

typedef struct mtranslate_s
{
	struct mtranslate_s *next;
	int gametype;
	qboolean bits;
	char key[64];
	char label[128];
	char names[256];
} mtranslate_t;

#define MENU_MAX_FRAMES 32

/* screen (SPEC-800/801): one level of the menu stack */
struct mscreen_s
{
	char name[MAX_QPATH];
	mframe_t *frames[MENU_MAX_FRAMES];
	int numframes;
	mtint_t *tints;
	mtranslate_t *translates;

	float scale;             /* s */
	qboolean resize;
	int originx, originy;    /* window origin for drag (resize screens) */
	qboolean nopush, ready, fragile, invalid;
	qboolean hasrail;        /* MENU-SCRATCH: composed with the navigation rail */
	qboolean wide;           /* MENU-SCRATCH: openMERC wide layout (m_wide.c) */
	mwgeom_t wg;             /* its geometry */

	mcolor_t tiptint, tipbtint;
	char tipfont[MENU_FONT_LEN];

	int opentime;            /* ms */
	qboolean hastimeout;
	float timeout;
	char *timeoutcmd;

	char config[MAX_QPATH];
	char exitcfg[MAX_QPATH];
	char onexitcvar[64];
	char onexitmenu[MAX_QPATH];

	int cursor;              /* screen pointer setting */
	qboolean controlmodel;   /* a model with "control" is on the screen */

	mwidget_t **tablist;
	int numtab;
	qboolean tabdirty;
	int lasttab;

	mwidget_t *selections[4];
	qboolean dragging;
	int dragx, dragy;
};

/* parse context of one page (SPEC-800 sections 1-4) */
struct mparse_s
{
	char *buf;
	int len, cap, pos, lastpos;
	int line;
	char tag[64];
	mpage_t *page;
	mscreen_t *screen;
	int expansions;
	qboolean dry;           /* parse test: no side effects */
	qboolean fragment;      /* MENU2 parse test: a fragment parsed alone (no including page) */
};

/* menu system state (SPEC-801) */
typedef struct
{
	qboolean active;
	qboolean forced;
	mscreen_t *stack[MENU_MAX_STACK];
	int depth;

	int now;                 /* ms, sampled each menu frame */
	int created;             /* ms, menu system created */
	int lastpop;             /* ms, last pop */
	float mx, my;            /* pointer */
	int cursorv;             /* keyboard pointer speed */
	mwidget_t *capture;      /* widget holding the keyboard */

	/* tooltip */
	mwidget_t *tipwidget;
	char tiptext[512];
	int tipx, tipy, tipstart;
	qboolean tipsound;
	mwidget_t *lasthover;

	qboolean drawing;
	int framecount;
} menustate_t;

extern menustate_t ms;

/* cvars */
extern cvar_t *menu_mouse_x, *menu_mouse_y;
extern cvar_t *m_cursor_speed, *m_cursor_accel, *m_sensitivity_wheel;
extern cvar_t *tip_duration, *cursor_blink_speed, *ghl_no_texture_stack;
extern cvar_t *scr_menuoverlay, *m_doublespeed;

/* ---- m_parse.c (SPEC-800 sections 1-5) ---- */
mpage_t *MP_LoadPage(mscreen_t *scr, mframe_t *frame, const char *name, qboolean dry);
void MP_FreePage(mpage_t *pg);
int MP_Token(mparse_t *p, char *out);
void MP_Unread(mparse_t *p);
mframe_t *MP_FindFrame(mscreen_t *scr, const char *name);
qboolean MP_ShowPage(mscreen_t *scr, mframe_t *f, const char *name, qboolean dry);
void MP_ParseText(mparse_t *p);
void MP_InitPage(mpage_t *pg, mscreen_t *scr, mframe_t *frame, const char *name);
qboolean MP_Args(mparse_t *p, char out[][MENU_MAX_TOKEN], int n);
void MP_EndTag(mparse_t *p);
void MP_SkipTag(mparse_t *p);
void MP_Error(mparse_t *p, const char *fmt, ...);
mcolor_t MP_Color(mscreen_t *scr, const char *arg);
int MP_ScaleInt(mscreen_t *scr, float v);
const char *MP_ExpandMenuName(const char *name);
qboolean MP_ReadFile(const char *path, char **text, int *len);
void MP_ExecConfig(const char *name);
extern qboolean mp_rebuilding; /* set while M_RebuildScreens re-creates screens */

/* ---- m_layout.c (SPEC-800 sections 6-10) ---- */
mwidget_t *ML_NewWidget(mparse_t *p, const mwclass_t *cls, qboolean pagefont);
void ML_ReadOptions(mparse_t *p, mwidget_t *w);
qboolean ML_CommonOption(mparse_t *p, mwidget_t *w, const char *kw);
void ML_AddWidget(mparse_t *p, mwidget_t *w);
void ML_FreeWidget(mwidget_t *w);
void ML_BreakLine(mpage_t *pg, qboolean hbr);
void ML_SetAlign(mpage_t *pg, int align);
int ML_TotalW(const mwidget_t *w);
int ML_TotalH(const mwidget_t *w);
int ML_ContentW(const mpage_t *pg);
int ML_ContentH(const mpage_t *pg);
int ML_FontHeight(const char *font);
void ML_WidgetScreenPos(const mpage_t *pg, const mwidget_t *w, int *x, int *y);
void ML_PageMouse(const mpage_t *pg, const mwidget_t *w, float *mx, float *my);
void ML_PageToScreen(const mpage_t *pg, const mwidget_t *w, float *x, float *y);
void ML_DrawFrame(mscreen_t *scr, mframe_t *f);
void ML_DrawBorder(int x, int y, int w, int h, int pad, int thick, mcolor_t c, qboolean fill, mcolor_t fillc);
qboolean ML_RunKeyList(mpage_t *pg, mbind_t *b, int key, qboolean sound);
mbind_t *ML_NewBind(int key, const char *cmd0, const char *cmd1, const char *cvar, qboolean ckey);
void ML_AddIKey(mbind_t **list, const char *bound, const char *cmd);
void ML_FreeBinds(mbind_t *b);
void ML_ClampScroll(mpage_t *pg);
void ML_Reposition(mpage_t *pg, mwidget_t *w);

/* ---- m_widgets.c (SPEC-802) ---- */
qboolean MW_Tag(mparse_t *p, const char *tag);
void MW_BareText(mparse_t *p, const char *text);
void MW_Upper(char *s);
void MW_ListedCase(char *name, const char *full, const strlist_t *paks); /* SPEC-AMEND-005 I6m.3 */
void MW_LabelValue(mpage_t *pg, mwidget_t *w, int x, int y, const char *label, const char *value,
		qboolean blink, qboolean disabled);
qboolean MW_Disabled(const mwidget_t *w);
void MW_SplitComma(const char *text, char ***items, int *count);
void MW_FreeList(char **items, int count);
void MW_SelectCommand(int argc);

/* ---- m_data.c (SPEC-803) ---- */
qboolean MD_DataTag(mparse_t *p, const char *tag);
void MS_ClearServers(void);
void MS_ServerFrame(void);
void MS_ServerAck(netadr_t adr, int sent);
void M_PingServers(void);
void MS_ServerPrint(netadr_t adr, const char *text);
void MS_Init(void);

/* ---- m_model.c (Ghoul widgets of SPEC-803 section 1/2) ---- */
qboolean MM_Tag(mparse_t *p, const char *tag);
void MM_DrawBackground(mpage_t *pg);
void MM_FreeModel(mmodel_t *m);
qboolean MM_BoltOption(mparse_t *p, mwidget_t *w, const char *name, qboolean bbolt);
void MM_UpdateBolt(mpage_t *pg, mwidget_t *w);
void MM_Dump(mpage_t *pg);

/* ---- menu.c (SPEC-801) ---- */
void M_QueueCommand(const char *text, qboolean forced);
void M_PlaySound(const char *name);
void M_ExecNow(const char *text);
mscreen_t *M_TopScreen(void);
void M_MarkTabDirty(mscreen_t *scr);
void M_Popup(mpage_t *pg);
qboolean M_KeyIsDown(int key);
/* MENU-SCRATCH: newest savegame (landing page, "om_continue"); registers its
   screenshot as the picture "pics/menus/om/newest" */
qboolean M_NewestSave(char *name, size_t namesize, char *level, size_t levelsize,
		char *time, size_t timesize);
int M_Time(void);

/* ---- m_strings.c (string-package shim, SPEC-131/313) ---- */
void MSP_Register(const char *name);
const char *MSP_Find(const char *ref);

/* MENU-SCRATCH: openMERC wide layout (m_wide.c) */
#define MWD_NAVW 204
#define MWD_NAVPAD 14
#define MWD_MARGIN 10
qboolean MWD_Enabled(void);
qboolean MWD_ScreenWide(const char *name);
qboolean MWD_NavFits(void);
void MWD_Layout(mscreen_t *scr, qboolean nav);
void MWD_DefaultFrame(mscreen_t *scr, mframe_t *f);
qboolean MWD_Frame(mscreen_t *scr, mframe_t *f, const char *cut, int W, int H, qboolean *nopage);
void MWD_Chrome(const char *name, int x, int y, int w, int h, int hover);
void MWD_DrawBackground(int x, int y, int w, int h);
void MWD_DrawChrome(mscreen_t *scr);
qboolean MWD_FrameWell(mscreen_t *scr, mframe_t *f, int x, int y, int w, int h);
qboolean MWD_PageWide(const mpage_t *pg);
qboolean MWD_BoltSlot(mparse_t *p, mwidget_t *w, const char *name);
void MWD_PlacePage(mpage_t *pg);
void MWD_PlaceSlot(mpage_t *pg, mwidget_t *w);
qboolean MWD_DrawSlot(mpage_t *pg, mwidget_t *w, int x, int y);
const char *MW_ImagePic(const mwidget_t *w); /* m_widgets.c: pic of an <image>, else NULL */
const char *MW_ImageLabel(const mwidget_t *w); /* m_widgets.c: own tab caption (label option), else NULL */
void MSP_ReleaseMenu(void);
void MSP_Init(void);

/* ---- m_test.c ---- */
void MT_Init(void);
void MT_FrameEnd(void);
void MT_Frame(qboolean drawn);

int M_DialogCentreX(mscreen_t *scr); /* MENU-SCRATCH: x centre of the panel under a dialog */

#endif
