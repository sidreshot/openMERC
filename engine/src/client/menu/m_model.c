/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc: Ghoul models in menus - <ghoul>, <bghoul>, <gpm>, the bolt
 * options of every widget, the per-frame model update (playback, transform,
 * EOS / sound notes, bolt positions).
 *
 * Models come from the engine's Ghoul system (m_ghoul.cpp). I1c: they are drawn
 * in the renderer's menu-model mode (SPEC-002 slots 0xb8/0xb4 with the Ghoul GL
 * table of package I4b) through cl_ghoul.c when the menus draw with the
 * renderer's 2D table (om_menusoft 0); the software layer draws none.
 *
 * Interim behaviour while a model cannot be loaded (never with the stock data
 * and a working Ghoul system):
 *  - a widget whose bolt cannot be resolved stays in the flow layout;
 *  - a model with `control` that fails to load does not hold its screen in
 *    the waitanim state, and its `end` command is queued at once;
 *  - <gpm> keeps a data-only widget that releases the arrow-button guard
 *    (`menu_semaphore` := 0 every frame, SPEC-803 section 2 step 3).
 *
 * Implements: SPEC-803 (sections 1 and 2), SPEC-800 (section 7 bolt/bbolt); SPEC-AMEND-005 I6m.6 (whose events)
 */

#include "header/menu.h"
#include "header/m_ghoul.h"
#include "header/m_draw.h"            /* I1c: menu-model mode of the renderer */
#include "../header/cl_ghoul.h"       /* I1c: drawing through the Ghoul system */

struct mmodel_s
{
	char name[MAX_QPATH];
	mgmodel_t *g;
	mpage_t *page;
	mwidget_t *widget;      /* placed <ghoul>, NULL for <bghoul> and bolted models */
	mmodel_t *parent;       /* gbolt / bgbolt parent */
	qboolean background;
	qboolean oneshot, control, animate, hastime;
	float time, scale;
	char end[256];
	float rotate[3], rotation[3];
	char yaw[64], pitch[64], roll[64];
	char gbolt[2][64];
	char cvar2[64];
	qboolean bgbolt;
	int xoff, yoff;
	qboolean started;
	int sx, sy;             /* screen point */
	float light;            /* MENU-SCRATCH: > 0: a key and ambient light of this strength (no light nodes) */
};

static float
MM_Clock(void)
{
	return M_Time() * 0.001f;
}

/* ------------------------------------------------------------------ */

void
MM_FreeModel(mmodel_t *m)
{
	if (m)
	{
		MG_Free(m->g);
		Z_Free(m);
	}
}

static mmodel_t *
MM_Root(mmodel_t *m)
{
	while (m && m->parent)
	{
		m = m->parent;
	}

	return m;
}

static void
MM_Note(void *ctx, int kind, const char *arg)
{
	mmodel_t *m = ctx;
	mscreen_t *scr = m->page ? m->page->screen : NULL;

	if (kind == MG_NOTE_SOUND)
	{
		if (arg && arg[0])
		{
			M_PlaySound(va("%s.wav", arg));
		}

		return;
	}

	if (kind == MG_NOTE_EOS)
	{
		if (m->control && scr)
		{
			scr->ready = true;
		}

		if (m->end[0])
		{
			M_QueueCommand(m->end, true);
		}
	}
}

/* screen point of a root model */
static void
MM_ScreenPoint(mmodel_t *m)
{
	mpage_t *pg = m->page;
	mframe_t *f = pg->frame;

	if (m->widget)
	{
		int x, y;

		ML_WidgetScreenPos(pg, m->widget, &x, &y);
		/* ML_WidgetScreenPos already adds the widget offsets */
		m->sx = x;
		m->sy = y;
		return;
	}

	m->sx = f->fx + f->pad + pg->screen->originx + m->xoff;
	m->sy = f->fy + f->pad + pg->screen->originy + m->yoff;
}

static void
MM_Update(mmodel_t *m)
{
	float now = MM_Clock(), ang[3];
	int i;

	if (!m->g)
	{
		return;
	}

	if (!m->started)
	{
		m->started = true;

		/* time F: static pose at F s; oneshot: played once and held; else looped */
		if (m->hastime)
		{
			MG_Play(m->g, now, 3, m->time);
		}
		else
		{
			MG_Play(m->g, now, m->oneshot ? 1 : 0, 0);
		}
	}

	for (i = 0; i < 3; i++)
	{
		ang[i] = m->rotation[i] + m->rotate[i] * (float)Sys_Milliseconds();
	}

	if (m->roll[0])
	{
		ang[0] += (float)fmod(Cvar_VariableValue(m->roll), 2 * M_PI);
	}

	if (m->yaw[0])
	{
		ang[1] += (float)fmod(Cvar_VariableValue(m->yaw), 2 * M_PI);
	}

	if (m->pitch[0])
	{
		ang[2] += (float)fmod(Cvar_VariableValue(m->pitch), 2 * M_PI);
	}

	MG_SetTransform(m->g, m->scale, ang);

	if (!m->parent)
	{
		MM_ScreenPoint(m);

		/* SPEC-AMEND-005 I6m.6: a model's events (sound notes, EOS with control/end) are processed only while its
		   own screen is not ready (a waitanim screen still waiting); bolt-ons are processed through their parent */
		if (m->page && m->page->screen && !m->page->screen->ready)
		{
			MG_ProcessEvents(m->g, now);
		}
	}
}

/* I1c: SPEC-803 section 1 Rendering - a root model with its bolt-ons (models attached with gbolt/bgbolt are
   drawn by their parent) inside the renderer's menu-model mode; with a `forced_camera` part the view is
   that part's bolt matrix */
static void
MM_Draw(mmodel_t *m)
{
	int cam;

	if (!m || !m->g || m->parent)
	{
		return;
	}

	cam = MG_FindPart(m->g, "forced_camera");

	if (MR_ModelBegin(cam != 0))
	{
		if (m->light > 0 && !cam)
		{
			CL_Ghoul_DrawMenuModelLit(MG_Instance(m->g), (float)m->sx, (float)m->sy, MM_Clock(), m->light);
		}
		else
		{
			CL_Ghoul_DrawMenuModel(MG_Instance(m->g), (float)m->sx, (float)m->sy, MM_Clock(), cam);
		}

		MR_ModelEnd(cam != 0);
	}
}

/* background models of a page: updated after the backdrop, before the items */
void
MM_DrawBackground(mpage_t *pg)
{
	int i;

	for (i = 0; i < pg->nummodels; i++)
	{
		if (!pg->models[i]->widget)
		{
			MM_Update(pg->models[i]);
			MM_Draw(pg->models[i]); /* I1c */
		}
	}
}

/* bolted item: its position follows the bolt every frame (SPEC-803 section 1) */
void
MM_UpdateBolt(mpage_t *pg, mwidget_t *w)
{
	mmodel_t *m = w->boltmodel, *root = MM_Root(m);
	float p[3];

	(void)pg;

	if (!m || !m->g || !root)
	{
		return;
	}

	MG_BoltPos(m->g, w->bolt, MM_Clock(), p);
	w->boltx = root->sx + (int)p[0];
	w->bolty = root->sy + (int)p[1];
	w->boltfacing = true;
}

qboolean
MM_BoltOption(mparse_t *p, mwidget_t *w, const char *name, qboolean bbolt)
{
	mmodel_t *m = bbolt ? p->page->firstbg : p->page->curmodel;
	int id;

	/* MENU-SCRATCH: no folder model in the wide layout, a slot instead */
	if (MWD_PageWide(p->page))
	{
		return MWD_BoltSlot(p, w, name);
	}

	if (!m || !m->g)
	{
		/* MENU2: a fragment parsed alone (om_rmftest) has no including page,
		   whose model it bolts to */
		if (!p->fragment)
		{
			MP_Error(p, "No ghoul model to bolt to");
		}

		return false;
	}

	id = MG_FindPart(m->g, name);

	if (!id)
	{
		MP_Error(p, "Could not find part to bolt to");
		return false;
	}

	w->boltmodel = m;
	w->bolt = id;
	w->flags |= WF_BOLTED | (bbolt ? WF_BBOLT : 0);
	return true;
}

static qboolean
MM_ModelOption(mparse_t *p, mmodel_t *m, mwidget_t *w, const char *kw)
{
	char a[3][MENU_MAX_TOKEN];

	if (!Q_stricmp(kw, "yaw"))
	{
		MP_Args(p, a, 1);
		Q_strlcpy(m->yaw, a[0], sizeof(m->yaw));
	}
	else if (!Q_stricmp(kw, "pitch"))
	{
		MP_Args(p, a, 1);
		Q_strlcpy(m->pitch, a[0], sizeof(m->pitch));
	}
	else if (!Q_stricmp(kw, "roll"))
	{
		MP_Args(p, a, 1);
		Q_strlcpy(m->roll, a[0], sizeof(m->roll));
	}
	else if (!Q_stricmp(kw, "scale"))
	{
		MP_Args(p, a, 1);
		/* F x s, or F alone for a gbolt / bgbolt model */
		m->scale = (float)atof(a[0]) * (m->gbolt[0][0] ? 1.0f : p->screen->scale);
		w->scale = m->scale;
	}
	else if (!Q_stricmp(kw, "oneshot"))
	{
		m->oneshot = true;
	}
	else if (!Q_stricmp(kw, "control"))
	{
		m->control = true;
	}
	else if (!Q_stricmp(kw, "animate"))
	{
		m->animate = true;
	}
	else if (!Q_stricmp(kw, "time"))
	{
		MP_Args(p, a, 1);
		m->time = (float)atof(a[0]);
		m->hastime = true;
	}
	else if (!Q_stricmp(kw, "rotate") || !Q_stricmp(kw, "rotation"))
	{
		float *v = !Q_stricmp(kw, "rotate") ? m->rotate : m->rotation;

		MP_Args(p, a, 3);
		v[0] = (float)atof(a[0]);
		v[1] = (float)atof(a[1]);
		v[2] = (float)atof(a[2]);
	}
	else if (!Q_stricmp(kw, "mousex") || !Q_stricmp(kw, "mousey"))
	{
		MP_Args(p, a, 2);
		w->flags |= WF_INTERACTIVE;
	}
	else if (!Q_stricmp(kw, "gbolt") || !Q_stricmp(kw, "bgbolt"))
	{
		MP_Args(p, a, 2);
		Q_strlcpy(m->gbolt[0], a[0], sizeof(m->gbolt[0]));
		Q_strlcpy(m->gbolt[1], a[1], sizeof(m->gbolt[1]));
		m->bgbolt = !Q_stricmp(kw, "bgbolt");
		m->scale = 1.0f;
		w->scale = 1.0f;
	}
	else if (!Q_stricmp(kw, "end"))
	{
		MP_Args(p, a, 1);
		Q_strlcpy(m->end, a[0], sizeof(m->end));
	}
	else if (!Q_stricmp(kw, "cvar2"))
	{
		MP_Args(p, a, 1);
		Q_strlcpy(m->cvar2, a[0], sizeof(m->cvar2));

		if (!p->dry)
		{
			Cvar_Get(a[0], "0", 0);
		}
	}
	else
	{
		return false;
	}

	return true;
}

/* options of a model tag: own options first, then the common ones; the
   common xoff/yoff are multiplied by the scale current at that moment */
static void
MM_ReadOptions(mparse_t *p, mmodel_t *m, mwidget_t *w)
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

		if (MM_ModelOption(p, m, w, kw) || ML_CommonOption(p, w, kw))
		{
			continue;
		}

		MP_Error(p, "Unrecognised ghoul keyword");
		return;
	}
}

static void
MM_Missing(mparse_t *p, mmodel_t *m)
{
	MP_Error(p, "Invalid ghoul model");

	if (p->dry)
	{
		return;
	}

	/* stand-in for the end of the animation that cannot play */
	if (m->end[0])
	{
		M_QueueCommand(m->end, true);
	}
}

static void
GhoulW_Update(mpage_t *pg, mwidget_t *w)
{
	(void)pg;

	if (w->data)
	{
		MM_Update(w->data);
	}
}

static void
GhoulW_Free(mwidget_t *w)
{
	/* the model belongs to the page (pg->models) */
	w->data = NULL;
}

/* I1c: a placed model is drawn in the widget order; one attached with `bolt`/`bbolt` only while its bolt
   faces the viewer (SPEC-803 section 1) */
static void
GhoulW_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	(void)pg;
	(void)x;
	(void)y;

	if (w->data && (!(w->flags & WF_BOLTED) || w->boltfacing))
	{
		MM_Draw(w->data);
	}
}

static const mwclass_t mm_ghoulclass = {
	"ghoul", NULL, NULL, NULL, NULL, GhoulW_Update, GhoulW_Draw, NULL, NULL, GhoulW_Free /* I1c: draw */
};

/* <gpm>: player-model catalogue and the cvars of the player setup page
   (SPEC-803 section 2); the preview model itself is not drawn yet */
#define GPM_MAX 128

typedef struct
{
	char skincvar[64], teamcvar[64], cvar2[64];
	qboolean single;
	int count, numteams;
	char names[GPM_MAX][64];       /* file base names */
	int team[GPM_MAX];             /* team index of each model */
	char teams[GPM_MAX][64];
	int teamfirst[GPM_MAX];
	int shownteam, shownmodel;
	qboolean first;                /* the next rebuild looks up the skin cvar */
	mmodel_t *mm;                  /* I1c: the tag's model options and the built model (mm->g) */
	mmodel_t *parent;              /* I1c: the gbolt / bgbolt parent */
} gpm_t;

static void
GPM_Catalogue(gpm_t *d, const char *only)
{
	strlist_t files;
	int i, j;

	d->count = d->numteams = 0;
	files = FS_ListFiles2(only ? va("ghoul/pmodels/%s.gpm", only) : "ghoul/pmodels/*.gpm", 0, SFF_SUBDIR);

	for (i = 0; i < files.num && d->count < GPM_MAX; i++)
	{
		char base[64], team[64], *dot;
		const char *slash = strrchr(files.data[i], '/');

		Q_strlcpy(base, slash ? slash + 1 : files.data[i], sizeof(base));
		MW_Upper(base);

		if (!MG_PlayerTeam(base, team, sizeof(team)))
		{
			continue; /* files that fail to load are skipped */
		}

		dot = strrchr(base, '.');

		if (dot)
		{
			*dot = 0;
		}

		for (j = 0; j < d->numteams; j++)
		{
			if (!strcmp(d->teams[j], team))
			{
				break;
			}
		}

		if (j == d->numteams && d->numteams < GPM_MAX)
		{
			Q_strlcpy(d->teams[j], team, sizeof(d->teams[j]));
			d->teamfirst[j] = d->count;
			d->numteams++;
		}

		Q_strlcpy(d->names[d->count], base, sizeof(d->names[0]));
		d->team[d->count] = j;
		d->count++;
	}

	StrList_Free(&files);
}

static int
GPM_Wrap(int v, int n)
{
	if (n <= 0)
	{
		return 0;
	}

	if (v < 0)
	{
		return n - 1;
	}

	return v >= n ? 0 : v;
}

static void GPM_Fit(mwidget_t *w, gpm_t *d);

static void
GPM_Rebuild(mwidget_t *w, gpm_t *d, int model)
{
	int i;

	if (d->first)
	{
		const char *skin = d->skincvar[0] ? Cvar_VariableString(d->skincvar) : "";

		d->first = false;

		for (i = 0; i < d->count; i++)
		{
			if (!Q_stricmp(d->names[i], skin))
			{
				model = i;
				break;
			}
		}
	}

	if (model < 0 || model >= d->count)
	{
		return;
	}

	d->shownmodel = model;
	d->shownteam = d->team[model];

	/* I1c: the model itself (SPEC-803 section 2 Rebuilding), re-attached to its gbolt / bgbolt parent */
	if (d->mm)
	{
		MG_Free(d->mm->g);
		d->mm->g = MG_LoadPlayer(va("%s.gpm", d->names[model]), MM_Clock());
		d->mm->started = false;
		GPM_Fit(w, d);

		if (d->mm->g && d->parent && d->parent->g && d->mm->gbolt[0][0])
		{
			MG_Attach(d->parent->g, MG_FindPart(d->parent->g, d->mm->gbolt[0]), d->mm->g,
					MG_FindPart(d->mm->g, d->mm->gbolt[1]));
		}
	}

	if (d->single)
	{
		return;
	}

	if (w->cvar)
	{
		Cvar_SetValue(w->cvar->name, (float)model);
	}

	if (d->skincvar[0])
	{
		Cvar_Set(d->skincvar, d->names[model]);
	}

	if (d->teamcvar[0])
	{
		Cvar_Set(d->teamcvar, d->teams[d->shownteam]);
	}

	if (d->cvar2[0])
	{
		Cvar_SetValue(d->cvar2, (float)d->shownteam);
		char name[80];

		Com_sprintf(name, sizeof(name), "%s_image", d->cvar2);
		Cvar_Set(name, va("teamicons/%s", d->teams[d->shownteam]));
	}
}

/* MENU-SCRATCH: a <gpm> without gbolt/bgbolt (own player page, no stock
   panel model) is a placed preview: the model is fitted into the widget's
   width x height after every rebuild and drawn in the widget order */
static void
GPM_Fit(mwidget_t *w, gpm_t *d)
{
	mmodel_t *m = d->mm;
	float mins[3], maxs[3], turn[3], dx, dy, s, W, H;
	int k;

	if (!m || !m->g || d->parent)
	{
		return;
	}

	for (k = 0; k < 3; k++)
	{
		turn[k] = m->rotation[k] + (m->rotate[k] != 0.0f ? (float)(M_PI / 4.0) : 0.0f);
	}

	MG_SetTransform(m->g, 1.0f, turn);
	MG_Play(m->g, 0, 3, 0);
	MG_Bounds(m->g, 0, mins, maxs);
	m->started = false;

	dx = maxs[0] - mins[0];
	dy = maxs[1] - mins[1];

	if (dx < 1.0f || dy < 1.0f)
	{
		return;
	}

	W = w->w * w->scale;
	H = w->h * w->scale;
	s = W / dx < H / dy ? W / dx : H / dy;
	s *= 0.94f;
	m->scale = s;
	w->xoff = (int)((W - dx * s) / 2 - mins[0] * s);
	w->yoff = (int)((H - dy * s) / 2 - mins[1] * s);
}

static void
GPM_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	gpm_t *d = w->data;

	(void)pg;
	(void)x;
	(void)y;

	if (d && d->mm && d->mm->g && !d->parent)
	{
		MM_Draw(d->mm);
	}
}

static void
GPM_Update(mpage_t *pg, mwidget_t *w)
{
	gpm_t *d = w->data;

	(void)pg;

	if (d && d->count > 0)
	{
		if (d->cvar2[0])
		{
			int t = GPM_Wrap((int)Cvar_VariableValue(d->cvar2), d->numteams);

			if (t != d->shownteam)
			{
				GPM_Rebuild(w, d, d->teamfirst[t]);
			}
		}

		if (w->cvar)
		{
			int m = GPM_Wrap((int)w->cvar->value, d->count);

			if (m != d->shownmodel && d->shownmodel >= 0)
			{
				int dir = ((int)w->cvar->value > d->shownmodel) ? 1 : -1, guard;
				const char *team = d->teamcvar[0] ? Cvar_VariableString(d->teamcvar) : NULL;

				/* step in the direction of the change to a model of the current team */
				for (guard = 0; guard < d->count && team && Q_stricmp(d->teams[d->team[m]], team); guard++)
				{
					m = GPM_Wrap(m + dir, d->count);
				}

				GPM_Rebuild(w, d, m);
			}
			else if (d->shownmodel < 0)
			{
				GPM_Rebuild(w, d, m);
			}
		}
		else if (d->shownmodel < 0)
		{
			GPM_Rebuild(w, d, 0);
		}
	}

	Cvar_Set("menu_semaphore", "0");

	/* I1c: playback and transform of the preview; it is drawn as a bolt-on of its parent */
	if (d && d->mm && d->mm->g)
	{
		MM_Update(d->mm);
	}
}

/* I1c: the built model goes with the widget */
static void
GPM_Free(mwidget_t *w)
{
	gpm_t *d = w->data;

	if (d)
	{
		if (d->mm)
		{
			MG_Free(d->mm->g);
			Z_Free(d->mm);
		}

		Z_Free(d);
	}

	w->data = NULL;
}

static void
GPM_Refresh(mpage_t *pg, mwidget_t *w)
{
	gpm_t *d = w->data;

	(void)pg;

	if (d)
	{
		d->first = true;
		d->shownmodel = d->shownteam = -1;
	}
}

static const mwclass_t mm_gpmclass = {
	"ghoul", NULL, NULL, NULL, GPM_Refresh, GPM_Update, GPM_Draw, NULL, NULL, GPM_Free /* I1c: freedata */
};

qboolean
MM_Tag(mparse_t *p, const char *tag)
{
	char a[2][MENU_MAX_TOKEN];
	mmodel_t *m;
	mwidget_t *w;
	mpage_t *pg = p->page;
	qboolean bg = !Q_stricmp(tag, "bghoul");
	qboolean gpm = !Q_stricmp(tag, "gpm");
	int noeos = 0;

	if (!bg && !gpm && Q_stricmp(tag, "ghoul"))
	{
		return false;
	}

	/* MENU-SCRATCH: the folder model and its decorations are not created in
	   the wide layout; own chrome replaces them (m_wide.c) */
	if (!gpm && MWD_PageWide(pg))
	{
		MP_SkipTag(p);
		return true;
	}

	if (gpm)
	{
		/* <gpm skin-cvar [team-cvar] ...>: the second argument is optional */
		char tok[MENU_MAX_TOKEN];

		MP_Args(p, a, 1);
		a[1][0] = 0;

		if (MP_Token(p, tok) == 1)
		{
			static const char *opts[] = {"cvari", "cvar2", "bgbolt", "gbolt", "scale", "rotate", "rotation",
				"yaw", "pitch", "roll", "time", "oneshot", "control", "animate", "end", "mousex", "mousey",
				"xoff", "yoff", "width", "height", "tint", "atint", "key", "tip", NULL};
			int i;

			for (i = 0; opts[i]; i++)
			{
				if (!Q_stricmp(tok, opts[i]))
				{
					break;
				}
			}

			if (opts[i])
			{
				MP_Unread(p);
			}
			else
			{
				Q_strlcpy(a[1], tok, sizeof(a[1]));
			}
		}
		else
		{
			MP_Unread(p);
		}
	}
	else
	{
		MP_Args(p, a, 1);
	}

	m = Z_Malloc(sizeof(*m));
	Q_strlcpy(m->name, a[0], sizeof(m->name));
	m->page = pg;
	m->background = bg;
	m->scale = p->screen->scale;

	w = ML_NewWidget(p, gpm ? &mm_gpmclass : &mm_ghoulclass, true);
	w->scale = m->scale;
	MM_ReadOptions(p, m, w);

	if (gpm)
	{
		gpm_t *d = Z_Malloc(sizeof(gpm_t));

		/* <gpm name>: only ghoul/pmodels/<name>.gpm, no cvars written */
		d->single = !a[1][0] && !w->cvar && !m->cvar2[0];

		if (!d->single)
		{
			Q_strlcpy(d->skincvar, a[0], sizeof(d->skincvar));
			Q_strlcpy(d->teamcvar, a[1], sizeof(d->teamcvar));
			Q_strlcpy(d->cvar2, m->cvar2, sizeof(d->cvar2));
		}

		d->first = true;
		d->shownmodel = d->shownteam = -1;
		w->data = d;

		if (!p->dry)
		{
			Cvar_Get("menu_semaphore", "0", 0);
			GPM_Catalogue(d, d->single ? a[0] : NULL);
		}

		/* I1c: the tag's options stay with the widget for the model it builds; it hangs on its gbolt /
		   bgbolt parent and is drawn by it */
		d->parent = m->gbolt[0][0] ? (m->bgbolt ? pg->firstbg : pg->curmodel) : NULL;
		m->parent = d->parent;
		d->mm = m;

		if (m->gbolt[0][0] || d->single || !w->w || !w->h)
		{
			w->flags |= WF_NOTPLACED;
		}
		else
		{
			/* MENU-SCRATCH: placed preview (GPM_Fit), lit by its own key light (the stock
			   panel model carried the light nodes) */
			m->light = 1.0f;
			w->scale = p->screen->scale;
			w->minh = w->h;
			m->widget = w;
		}
		ML_AddWidget(p, w);
		return true;
	}

	m->g = MG_Load(m->name, &noeos);

	if (!m->g)
	{
		MM_Missing(p, m);
		ML_FreeWidget(w);
		Z_Free(m);
		return true;
	}

	if (noeos)
	{
		MP_Error(p, "Could not find EOS note token");
	}

	MG_SetNoteHandler(m->g, MM_Note, m);
	m->xoff = w->xoff;
	m->yoff = w->yoff;

	/* bolted onto another model of the page */
	if (m->gbolt[0][0])
	{
		mmodel_t *parent = m->bgbolt ? pg->firstbg : pg->curmodel;
		int pb, cb;

		if (!parent)
		{
			/* MENU2: a fragment parsed alone by om_rmftest has no parent model
			   from its including page; not an error of the data */
			if (!p->fragment)
			{
				MP_Error(p, "Could not owning ghoul model");
			}

			MM_FreeModel(m);
			ML_FreeWidget(w);
			return true;
		}

		pb = MG_FindPart(parent->g, m->gbolt[0]);
		cb = MG_FindPart(m->g, m->gbolt[1]);

		if (!pb || !cb)
		{
			MP_Error(p, !pb ? "Could not find bolt %s" : "Could not find nut %s", !pb ? m->gbolt[0] : m->gbolt[1]);
			MP_Error(p, "Invalid ghoul model");
			MM_FreeModel(m);
			ML_FreeWidget(w);
			return true;
		}

		MG_Attach(parent->g, pb, m->g, cb);
		m->parent = parent;
	}
	else if (!bg)
	{
		/* posed at time 0; the box fills the item with its minimum corner at the top-left */
		float mins[3], maxs[3], turn[3];
		int k;

		/* SPEC-803 "Size and origin": each axis with a non-zero rotate rate is
		   turned by pi/4 for the bounds, to allow for the spin (MENU2) */
		for (k = 0; k < 3; k++)
		{
			turn[k] = m->rotation[k] + (m->rotate[k] != 0.0f ? (float)(M_PI / 4.0) : 0.0f);
		}

		MG_SetTransform(m->g, 1.0f, turn);
		MG_Play(m->g, 0, 3, 0);
		MG_Bounds(m->g, 0, mins, maxs);

		if (!w->xoff)
		{
			w->xoff = (int)(-mins[0] * m->scale);
		}

		if (!w->yoff)
		{
			w->yoff = (int)(-mins[1] * m->scale);
		}

		if (!w->w)
		{
			w->w = (int)(maxs[0] - mins[0]);
		}

		if (!w->h)
		{
			w->h = (int)(maxs[1] - mins[1]);
		}

		m->widget = w;
	}

	if (pg->nummodels < 64)
	{
		pg->models[pg->nummodels++] = m;
	}
	else
	{
		MM_FreeModel(m);
		ML_FreeWidget(w);
		return true;
	}

	pg->curmodel = m;

	if (bg && !pg->firstbg)
	{
		pg->firstbg = m;
	}

	if (m->control)
	{
		p->screen->controlmodel = true;
	}

	if (m->widget)
	{
		int before = pg->numwidgets;

		w->data = m;
		ML_AddWidget(p, w);

		if (pg->numwidgets == before)
		{
			/* not created (condition, too wide, after a variable widget) */
			m->widget = NULL;
		}
	}
	else
	{
		/* background and bolted models are not placed; they are updated by the page */
		ML_FreeWidget(w);
	}

	return true;
}

/* test dump (om_menudump) */
void
MM_Dump(mpage_t *pg)
{
	int i;

	for (i = 0; i < pg->nummodels; i++)
	{
		mmodel_t *m = pg->models[i];

		Com_Printf("  model %s%s%s duration %.2f s (once) point %d %d\n", m->name, m->background ? " bg" : "",
				m->control ? " control" : "", MG_Duration(m->g, 1), m->sx, m->sy);
	}
}
