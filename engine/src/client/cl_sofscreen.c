/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Soldier of Fortune 1.07f screen texts and diagnostics: centre prints,
 * captions, typewriter prints, credit pictures, the pause screen, the
 * loading-screen progress, the debug graphs and the console diagnostics
 * (cl_showfps, cl_showlocation). The screen update order itself is in
 * cl_screen.c (SCR_UpdateScreen).
 *
 * Implements: SPEC-310 (sections 6, 7, 9, 10, 11)
 */

#include "header/client.h"
#include "header/sofclient.h"
#include "input/header/input.h"
#include "../common/header/sof/refapi.h"
#include "../common/header/sof/engine.h"
#include "../common/header/sof/savegame.h"

/* menu/m_draw.c: 128 x 128 RGBA of the drawn frame (renderer slot 0xd4) */
qboolean MR_Thumbnail(byte *out128x128);

static cvar_t *cinem_centertime;
static cvar_t *scr_centertime_c;
static cvar_t *scr_showpause_c;
static cvar_t *cl_showlocation;
static cvar_t *cl_showfps_c;
static cvar_t *graphheight, *graphscale, *graphshift, *graphwidth, *graphsize;
static cvar_t *netgraph, *timegraph, *debuggraph;

static const unsigned char c_yellow[4] = {255, 255, 0, 255};

/* ---------------------------------------------------------------------
 * centre print and caption (SPEC-310 section 6)
 * --------------------------------------------------------------------- */

#define PRINT_MAXLINES 10

static struct
{
	char lines[PRINT_MAXLINES][256];
	int n;
	int caption;          /* 0 centre print, 1 caption */
	int endtime;          /* cls.realtime when it disappears */
} cprint;

static void
SCR_EchoSeparator(void)
{
	Com_Printf("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36"
			"\36\36\36\36\36\36\36\36\36\36\37\n\n");
}

static void
SCR_SetPrint(const char *str, int caption)
{
	int maxchars, spaces = 0, len, brk, i;
	const char *s;
	const sofps_t *ps = CL_SoF_CurrentPS();

	cprint.n = 0;

	if (!str || !str[0])
	{
		cprint.endtime = 0; /* a null text clears it */
		return;
	}

	maxchars = HUD_W() / 8 - ((ps->remote_type == 2) ? 4 : 34);

	if (maxchars < 8)
	{
		maxchars = 8;
	}

	if (maxchars > 255)
	{
		maxchars = 255;
	}

	SCR_EchoSeparator();
	Com_Printf("%s\n", str);
	SCR_EchoSeparator();
	Con_ClearNotify();

	for (s = str; *s; s++)
	{
		if (*s == ' ')
		{
			spaces++;
		}
	}

	s = str;

	while (*s && (cprint.n < PRINT_MAXLINES))
	{
		/* up to a newline, else break at the last space before maxchars */
		for (len = 0; s[len] && (s[len] != '\n') && (len < maxchars); len++)
		{
		}

		brk = len;

		if (s[len] && (s[len] != '\n'))
		{
			for (i = len; i > 0; i--)
			{
				if (s[i] == ' ')
				{
					brk = i;
					break;
				}
			}
		}

		memcpy(cprint.lines[cprint.n], s, brk);
		cprint.lines[cprint.n][brk] = 0;
		cprint.n++;
		s += brk;

		if ((*s == '\n') || (*s == ' '))
		{
			s++;
		}
	}

	cprint.caption = caption;
	cprint.endtime = cls.realtime + (int)(1000 * (scr_centertime_c->value + 0.2f * spaces + 0.8f * cprint.n));
}

void
SCR_SoF_CenterPrint(const char *str)
{
	SCR_SetPrint(str, 0);
}

void
SCR_CaptionPrint(const char *str)
{
	SCR_SetPrint(str, 1);
}

static void
SCR_DrawPrint(void)
{
	int L, y0, i, H = HUD_H();
	const sofps_t *ps = CL_SoF_CurrentPS();

	if (!cprint.n || (cls.realtime >= cprint.endtime))
	{
		return;
	}

	L = HUD_FontHeight(1);

	if (!cprint.caption)
	{
		y0 = (int)(0.4f * H - (cprint.n / 2) * L);
	}
	else if (ps->remote_type == 2)
	{
		y0 = H - (int)(48.0f * H / 480) + 4;
	}
	else
	{
		y0 = (int)(0.9f * H - cprint.n * L);
	}

	for (i = 0; i < cprint.n; i++)
	{
		HUD_DrawFontText((HUD_W() - HUD_TextWidth(cprint.lines[i], 1)) / 2, y0 + i * L,
				cprint.lines[i], c_yellow, 1, i > 0);
	}
}

/* ---------------------------------------------------------------------
 * typewriter print (SPEC-310 section 6)
 * --------------------------------------------------------------------- */

static struct
{
	char text[1024];
	int len, x, y, speed;
	int endtime;
	int start;      /* client time of the first drawn frame, -1 = not drawn yet */
	int shown;      /* characters shown so far */
} tw;

void
SCR_TypewriterPrint(int x, int y, int speed, const char *str)
{
	Q_strlcpy(tw.text, str ? str : "", sizeof(tw.text));
	tw.len = (int)strlen(tw.text);
	tw.x = x;
	tw.y = y;
	tw.speed = speed;
	tw.start = -1;
	tw.shown = 0;
	tw.endtime = cls.realtime + (int)(1000 * (tw.len * 0.1f + cinem_centertime->value));
}

void
SCR_CancelTypewriter(void)
{
	tw.endtime = 0;
}

static void
SCR_DrawTypewriter(void)
{
	static const unsigned char col[4] = {0, 254, 0, 220};
	int speed, step, n, i, line = 0, col_i = 0, H = HUD_H();
	float y;

	if (!tw.endtime || (cls.realtime >= tw.endtime))
	{
		return;
	}

	if ((cls.state != ca_active) || (CL_SoF_CurrentPS()->remote_type != 2))
	{
		tw.endtime = 0; /* another view type clears it */
		return;
	}

	speed = (tw.speed < 1) ? 1 : (tw.speed > 10) ? 10 : tw.speed;
	step = ((10 - speed) > 1 ? (10 - speed) : 1) * 10;

	if (tw.start < 0)
	{
		tw.start = cl.time;
	}

	n = (cl.time - tw.start) / step + 1;

	if (n > tw.len)
	{
		n = tw.len;
	}

	if (n > tw.shown)
	{
		tw.shown = n;

		if (n < tw.len)
		{
			S_StartLocalSound("misc/type.wav");
		}
	}

	y = (float)(int)(H - tw.y * H / 640.0f);

	for (i = 0; i < n; i++)
	{
		if ((tw.text[i] == '\n') || (col_i >= 104))
		{
			line++;
			col_i = 0;

			if (tw.text[i] == '\n')
			{
				continue;
			}
		}

		HUD_DrawCharFloat(10.0f + 8 * col_i, y + 8 * line, (byte)tw.text[i], col);
		col_i++;
	}

	if (n < tw.len)
	{
		HUD_DrawCharFloat(10.0f + 8 * col_i, y + 8 * line, '_', col);
	}
}

/* ---------------------------------------------------------------------
 * credit picture (SPEC-310 section 6)
 * --------------------------------------------------------------------- */

static struct
{
	float xp, yp, time, fadein, fadeout;
	int r, g, b;
	char name[MAX_QPATH];
	int start;
} credit;

void
SCR_CreditPrint(const char *str)
{
	char buf[256], *f[9], *p;
	int i;

	Q_strlcpy(buf, str ? str : "", sizeof(buf));
	p = buf;

	for (i = 0; i < 9; i++)
	{
		f[i] = p;

		if (i < 8)
		{
			p = strchr(p, ';');

			if (!p)
			{
				credit.time = 0;
				return;
			}

			*p++ = 0;
		}
	}

	credit.xp = (float)atof(f[0]);
	credit.yp = (float)atof(f[1]);
	credit.time = (float)atof(f[2]);
	credit.fadein = (float)atof(f[3]);
	credit.fadeout = (float)atof(f[4]);
	credit.r = atoi(f[5]);
	credit.g = atoi(f[6]);
	credit.b = atoi(f[7]);
	Com_sprintf(credit.name, sizeof(credit.name), "pics/%s", f[8]);
	credit.start = cls.realtime;
}

static void
SCR_DrawCredit(void)
{
	float elapsed, remaining, a = 255;
	unsigned char col[4];
	int w, h;

	if (credit.time <= 0)
	{
		return;
	}

	elapsed = (cls.realtime - credit.start) * 0.001f;
	remaining = credit.time - elapsed;

	if (remaining <= 0)
	{
		credit.time = 0;
		return;
	}

	if ((credit.fadein > 0) && (elapsed < credit.fadein))
	{
		a = 255 * elapsed / credit.fadein;
	}
	else if ((credit.fadeout > 0) && (remaining < credit.fadeout))
	{
		a = 255 * remaining / credit.fadeout;
	}

	HUD_GetPicSize(&w, &h, credit.name);

	if ((w <= 0) || (h <= 0))
	{
		return;
	}

	col[0] = (unsigned char)credit.r;
	col[1] = (unsigned char)credit.g;
	col[2] = (unsigned char)credit.b;
	col[3] = (unsigned char)a;
	HUD_DrawPic((int)((HUD_W() - w) * credit.xp / 100), (int)((HUD_H() - h) * credit.yp / 100),
			credit.name, col);
}

void
SCR_ClearPrints(void)
{
	cprint.endtime = 0;
	tw.endtime = 0;
	credit.time = 0;
}

/* ---------------------------------------------------------------------
 * pause screen (SPEC-310 section 7)
 * --------------------------------------------------------------------- */

static void
SCR_SoF_DrawPause(void)
{
	static const unsigned char fade[4] = {0, 0, 0, 127};
	const char *s;
	int L;

	if (!scr_showpause_c->value || !cl_paused->value)
	{
		return;
	}

	HUD_DrawFade(fade);
	s = CL_StringById((4 << 8) | 8);
	L = HUD_FontHeight(2);
	HUD_DrawFontText((HUD_W() - HUD_TextWidth(s, 2)) / 2, (HUD_H() - L) / 2, s, c_yellow, 2, 0);
}

/* ---------------------------------------------------------------------
 * debug graphs (SPEC-310 section 11)
 * --------------------------------------------------------------------- */

#define GRAPH_SAMPLES 1024
#define GRAPH_SEGMENTS 5

typedef struct
{
	int n;
	float v[GRAPH_SEGMENTS];
	unsigned char c[GRAPH_SEGMENTS][4];
} graphsample_t;

static graphsample_t graph[GRAPH_SAMPLES];
static int graph_current;

static void
SCR_GraphSample(int n, const float *v, const unsigned char (*c)[4])
{
	graphsample_t *g = &graph[graph_current++ & (GRAPH_SAMPLES - 1)];
	int i;

	g->n = (n > GRAPH_SEGMENTS) ? GRAPH_SEGMENTS : n;

	for (i = 0; i < g->n; i++)
	{
		g->v[i] = v[i];
		memcpy(g->c[i], c[i], 4);
	}
}

static void
SCR_GraphValue(float v, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	unsigned char c[1][4];

	c[0][0] = r;
	c[0][1] = g;
	c[0][2] = b;
	c[0][3] = a;
	SCR_GraphSample(1, &v, (const unsigned char (*)[4])c);
}

/* network graph: one call per parsed server message */
void
SCR_SoF_AddNetgraph(void)
{
	int i, in, ping;

	if (debuggraph->value || timegraph->value || (host_speeds->value == 2))
	{
		return;
	}

	for (i = 0; i < cls.netchan.dropped; i++)
	{
		SCR_GraphValue(30, 64, 0, 0, 127);
	}

	for (i = 0; i < cl.surpressCount; i++)
	{
		SCR_GraphValue(30, 223, 0, 0, 127);
	}

	in = cls.netchan.incoming_acknowledged & (CMD_BACKUP - 1);
	ping = (cls.realtime - cl.cmd_time[in]) / 30;

	if (ping > 30)
	{
		ping = 30;
	}

	SCR_GraphValue((float)ping, 208, 0, 0, 127);
}

static void
SCR_SoF_DrawGraphs(void)
{
	int n, a, x, y, w, i, k;
	float height, base;

	if (timegraph->value)
	{
		SCR_GraphValue(cls.rframetime * 300, 255, 0, 0, 127);
	}

	if (host_speeds->value == 2)
	{
		static const unsigned char c[4][4] = {
			{255, 0, 0, 159}, {0, 255, 0, 159}, {0, 0, 255, 159}, {255, 0, 255, 159}
		};
		float v[4];

		v[0] = (float)(time_after_game - time_before_game);
		v[1] = 0;
		v[2] = 0;
		v[3] = (float)(time_after_ref - time_before_ref);
		SCR_GraphSample(4, v, c);
	}

	if (!debuggraph->value && !timegraph->value && !netgraph->value && (host_speeds->value != 2))
	{
		return;
	}

	w = (graphwidth->value >= 1) ? (int)graphwidth->value : 1;
	n = (int)graphsize->value;

	if (n > scr_vrect.width)
	{
		n = scr_vrect.width;
	}

	if (n > GRAPH_SAMPLES)
	{
		n = GRAPH_SAMPLES;
	}

	height = (graphheight->value > 0) ? graphheight->value : 100;
	x = scr_vrect.x + (scr_vrect.width - n * w) / 2;
	y = scr_vrect.y + scr_vrect.height;

	for (a = 0; a < n; a++)
	{
		graphsample_t *g = &graph[(graph_current - 1 - a) & (GRAPH_SAMPLES - 1)];
		int gx = x + (n - 1 - a) * w;

		base = 0;

		for (k = 0; k < g->n; k++)
		{
			float v = g->v[k] * graphscale->value + graphshift->value;

			if (v < 0)
			{
				v += height * (1 + (int)(-v / height));
			}

			i = (int)v % (int)height;
			HUD_DrawLine((float)gx, y - base, (float)gx, y - base - i, (float)w, g->c[k]);
			base += i;
		}
	}
}

/* ---------------------------------------------------------------------
 * loading screen (SPEC-310 section 9)
 * --------------------------------------------------------------------- */

void
SCR_SoF_LoadingProgress(float f)
{
	Cvar_SetValue("menu_loading", f);

	/* test aid: with cl_omshowframe each progress step is printed (SPEC-AMEND-005 I6h.5 frame count) */
	if (Cvar_VariableValue("cl_omshowframe"))
	{
		Com_Printf("OMLOAD %.4f\n", f);
	}

	if (!dedicated->value)
	{
		SCR_UpdateScreenForce();
	}
}

/* ---------------------------------------------------------------------
 * console diagnostics (SPEC-310 section 10)
 * --------------------------------------------------------------------- */

static void
SCR_SoF_DebugPrints(void)
{
	static float ft[10];
	static int fi;
	float sum = 0;
	int i;

	if (cl_showfps_c->value)
	{
		ft[fi % 10] = cls.rframetime;
		fi++;

		if ((cl_showfps_c->value == 2) || !(fi % 10))
		{
			for (i = 0; i < 10; i++)
			{
				sum += ft[i];
			}

			if (sum > 0)
			{
				Com_Printf("Frames Per Second = %f\n", 10.0f / sum);
			}
		}
	}

	if (cl_showlocation->value && (cls.state == ca_active) && cl_entities && (cl_numentities > 1))
	{
		const entity_state_t *e = &cl_entities[1].current;

		Com_Printf("Location: %.3f %.3f %.3f (Dir: %.3f)\n", e->origin[0], e->origin[1],
				e->origin[2], e->angles[YAW]);
	}
}

/* ---------------------------------------------------------------------
 * the 2D part of a SoF screen update, after the 3D view
 * --------------------------------------------------------------------- */

void
SCR_SoF_DrawOverlays(void)
{
	const sofps_t *ps = CL_SoF_CurrentPS();
	qboolean active = (cls.state == ca_active) && cl.refresh_prepped;

	/* OpenMerc V1: HUD scaling (scr_hudscale) - the HUD, prints and the pause text are drawn
	   in the virtual window; world labels and the debug graphs stay in window pixels */
	HUD_BeginScaled();

	if (active)
	{
		HUD_DrawCrosshair();
		HUD_DrawInterface();

		if (ps->stats[9] & 0x1)
		{
			HUD_DrawLayout();
			SCR_CancelTypewriter();
		}

		HUD_DrawRemoteOverlay();
	}

	SCR_DrawCredit();
	SCR_DrawPrint();
	SCR_DrawTypewriter();
	HUD_EndScaled();

	if (active && !(ps->stats[8] & 0x800))
	{
		HUD_DrawLabels();
	}

	SCR_SoF_DrawGraphs();
	HUD_BeginScaled();
	SCR_SoF_DrawPause();
	HUD_EndScaled();
}

void
SCR_SoF_DrawNet(void)
{
	if (((cls.state == ca_connected) || (cls.state == ca_active)) &&
		(cls.netchan.outgoing_sequence - cls.netchan.incoming_acknowledged > 62) && !CL_Demo_Playing())
	{
		HUD_BeginScaled(); /* OpenMerc V1: the view is the whole window in SoF mode */
		HUD_DrawPic(16, 16, "pics/console/net", (const unsigned char *)"\377\377\377\377");
		HUD_EndScaled();
	}
}

/* ---------------------------------------------------------------------
 * OpenMerc test captures (not in the original): `omshot <name>` and the
 * cvar cl_omshotframes "<frame> <frame> ..." save the finished frame
 * (before the buffer swap, through renderer slot 0xd4) as
 * <user>/scrnshot/<name>.tga; cl_omshowframe 1 prints the shown server
 * frame and entity 1's position (to pair frames with another engine).
 * --------------------------------------------------------------------- */

static char omshot_pending[MAX_QPATH];
static cvar_t *cl_omshotframes, *cl_omshotprefix, *cl_omshowframe, *cl_omexecactive;
static int omshot_lastframe = -1;

static void
SCR_OmShot_f(void)
{
	if (Cmd_Argc() != 2)
	{
		Com_Printf("usage: omshot <name>\n");
		return;
	}

	Q_strlcpy(omshot_pending, Cmd_Argv(1), sizeof(omshot_pending));
}

static void
SCR_OmCapture(const char *name)
{
	char path[MAX_OSPATH];
	unsigned char *rgba, *row, hdr[18];
	int w = viddef.width, h = viddef.height, i;
	FILE *f;
	extern const sofrefexport_t *HUD_Ref(void);
	const sofrefexport_t *re2 = HUD_Ref();

	if (!re2 || !re2->ReadThumbnail || (w <= 0) || (h <= 0))
	{
		Com_Printf("omshot: renderer has no readback slot\n");
		return;
	}

	rgba = malloc((size_t)w * h * 4);
	row = malloc((size_t)w * 3);

	if (!rgba || !row)
	{
		free(rgba);
		free(row);
		return;
	}

	re2->ReadThumbnail(rgba, w, h); /* rows bottom to top */

	Com_sprintf(path, sizeof(path), "%s/scrnshot/%s.tga", FS_Userdir(), name);
	FS_CreatePath(path);
	f = Q_fopen(path, "wb");

	if (f)
	{
		memset(hdr, 0, sizeof(hdr));
		hdr[2] = 2;
		hdr[12] = w & 255;
		hdr[13] = w >> 8;
		hdr[14] = h & 255;
		hdr[15] = h >> 8;
		hdr[16] = 24;
		fwrite(hdr, 1, 18, f);

		for (i = 0; i < h; i++)
		{
			const unsigned char *src = rgba + (size_t)i * w * 4;
			int x;

			for (x = 0; x < w; x++)
			{
				row[x * 3 + 0] = src[x * 4 + 2];
				row[x * 3 + 1] = src[x * 4 + 1];
				row[x * 3 + 2] = src[x * 4 + 0];
			}

			fwrite(row, 1, (size_t)w * 3, f);
		}

		fclose(f);
		Com_Printf("OMSHOTDONE %s frame %i\n", path, cl.frame.serverframe);
	}

	free(rgba);
	free(row);
}

/* OpenMerc V1 test helper: a centre print from the console (`om_centerprint <text>`) */
static void
SCR_OmCenterPrint_f(void)
{
	SCR_SoF_CenterPrint(Cmd_Args());
}

static void
SCR_OmCaptures(void)
{
	if (omshot_pending[0])
	{
		SCR_OmCapture(omshot_pending);
		omshot_pending[0] = 0;
	}

	if (cl_omshotframes->string[0] && (cls.state == ca_active) && cl.frame.valid &&
		(cl.frame.serverframe != omshot_lastframe))
	{
		char list[1024], *tok, *p, name[MAX_QPATH];

		omshot_lastframe = cl.frame.serverframe;
		Q_strlcpy(list, cl_omshotframes->string, sizeof(list));

		for (p = list; (tok = strtok(p, " ,")) != NULL; p = NULL)
		{
			if (atoi(tok) == cl.frame.serverframe)
			{
				Com_sprintf(name, sizeof(name), "%s_%i", cl_omshotprefix->string, cl.frame.serverframe);
				SCR_OmCapture(name);
				break;
			}
		}
	}

	/* cl_omexecactive <cfg>: exec a script once the client is in the level (a
	   script with waits started before would hold back the server's commands) */
	if ((cls.state == ca_active) && cl_omexecactive->string[0])
	{
		Cbuf_AddText(va("exec %s\n", cl_omexecactive->string));
		Cvar_Set("cl_omexecactive", "");
	}

	if (cl_omshowframe->value && (cls.state == ca_active) && cl_entities && (cl_numentities > 1))
	{
		const entity_state_t *e = &cl_entities[1].current;

		Com_Printf("OMFRAME %i %.3f %.3f %.3f %.3f\n", cl.frame.serverframe, e->origin[0],
				e->origin[1], e->origin[2], e->angles[YAW]);
	}
}

void
SCR_SoF_EndFrame(void)
{
	SCR_SoF_DebugPrints();
	SCR_OmCaptures();

	/* OpenMerc timed autosave: whether the game itself is on screen, and
	   its thumbnail when the server asked for one (before the swap) */
	SG_ClientEndFrame((cls.state == ca_active) && (cls.key_dest == key_game) &&
			cl.refresh_prepped && !cls.disable_screen, MR_Thumbnail);
}

void
SCR_SoF_Init(void)
{
	cinem_centertime = Cvar_Get("cinem_centertime", "2.5", 0);
	scr_centertime_c = Cvar_Get("scr_centertime", "2.5", 0);
	scr_showpause_c = Cvar_Get("scr_showpause", "1", 0);
	cl_showlocation = Cvar_Get("cl_showlocation", "0", 0);
	cl_showfps_c = Cvar_Get("cl_showfps", "0", 0);
	graphheight = Cvar_Get("graphheight", "100", 0);
	graphscale = Cvar_Get("graphscale", "1", 0);
	graphshift = Cvar_Get("graphshift", "0", 0);
	graphwidth = Cvar_Get("graphwidth", "4", 0);
	graphsize = Cvar_Get("graphsize", "50", 0);
	netgraph = Cvar_Get("netgraph", "0", 0);
	timegraph = Cvar_Get("timegraph", "0", 0);
	debuggraph = Cvar_Get("debuggraph", "0", 0);
	Cvar_Get("scr_menuoverlay", "0", 0);
	Cvar_Get("menu_loading", "0", 0);
	Cvar_Get("menu_nostats", "0", 0);
	Cvar_Get("cl_minfps", "5", 0);
	Cvar_Get("sc_framerate", "30", 0);

	cl_omshotframes = Cvar_Get("cl_omshotframes", "", 0);
	cl_omshotprefix = Cvar_Get("cl_omshotprefix", "om", 0);
	cl_omshowframe = Cvar_Get("cl_omshowframe", "0", 0);
	cl_omexecactive = Cvar_Get("cl_omexecactive", "", 0);
	Cmd_AddCommand("omshot", SCR_OmShot_f);
	Cmd_AddCommand("om_centerprint", SCR_OmCenterPrint_f); /* V1 test helper */

	HUD_Init();
}
