/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc: menu widgets whose content comes from the engine - lobby chat
 * (chat, users, rooms), server browser (serverbox, serverdetail, players,
 * translate), file and savegame choosers (filebox, filereq, loadbox), map
 * list and bots (listfile, botlist, bot), and the browser's server list.
 *
 * Implements: SPEC-803 (sections 3-6), SPEC-204 (section 1.4 hand-off)
 */

#include <sys/stat.h>
#include <time.h>

#include "header/menu.h"
#include "header/m_draw.h"
#include "../header/serverlist.h"
#include "../../common/header/sof/engine.h"
#include "../../common/header/sof/savegame.h"
#include "../../common/header/gamespy_chat.h" /* OpenMerc (I2d): MC_* prototypes */

static int
MD_FH(const mwidget_t *w)
{
	return MR_FontHeight(w->font);
}

static int
MD_FrameW(const mpage_t *pg)
{
	return ML_ContentW(pg);
}

/* row under the mouse (SPEC-802 row helpers): -1 when outside */
static int
MD_Row(mpage_t *pg, mwidget_t *w, int first, int *col)
{
	int x, y, mx, my, L = MD_FH(w);
	float fx, fy;

	ML_WidgetScreenPos(pg, w, &x, &y);
	ML_PageMouse(pg, w, &fx, &fy);
	mx = (int)fx - x;
	my = (int)fy - y;

	if (col)
	{
		*col = mx;
	}

	if (mx < 0 || mx > w->w || my < 0 || my > w->h || L <= 0)
	{
		return -1;
	}

	return my / L - first;
}

/* MENU-SCRATCH: a cell wider than its column ends in "..." instead of being
   cut mid-letter (long server host names) */
static const char *
MD_Fit(const char *s, int room, const char *font)
{
	static char buf[256];
	int lo, hi, dots;

	if (!s || room <= 0 || MR_StringWidth(s, font) <= room)
	{
		return s;
	}

	dots = MR_StringWidth("...", font);
	Q_strlcpy(buf, s, sizeof(buf));
	lo = 0;
	hi = (int)strlen(buf);

	/* longest prefix that fits with the dots */
	while (lo < hi)
	{
		int mid = (lo + hi + 1) / 2;
		char keep = buf[mid];

		buf[mid] = 0;

		if (MR_StringWidth(buf, font) + dots <= room)
		{
			lo = mid;
		}
		else
		{
			hi = mid - 1;
		}

		buf[mid] = keep;
	}

	while (lo > 0 && (buf[lo - 1] == ' ' || buf[lo - 1] == '^'))
	{
		lo--;
	}

	buf[lo] = 0;
	Q_strlcat(buf, "...", sizeof(buf));
	return buf;
}

static void
MD_Columns(mpage_t *pg, mwidget_t *w, int x, int y, const char **cells, int n, mcolor_t c)
{
	int j;

	for (j = 0; j < n; j++)
	{
		int x0 = x + (j < pg->numtabs ? pg->tabs[j] : 0);
		int x1 = (j + 1 < pg->numtabs) ? x + pg->tabs[j + 1] : x + ML_ContentW(pg);

		/* MENU-SCRATCH: a cell cut at the next column keeps a gap to it */
		if (j + 1 < n && x1 - x0 > 12)
		{
			x1 -= 6;
		}

		MR_SetClip(x0, pg->clip[1], x1 - x0, pg->clip[3]);
		MR_DrawString(x0, y, MD_Fit(cells[j], x1 - x0, w->font), c, w->font, 0);
	}

	MR_SetClip(pg->clip[0], pg->clip[1], pg->clip[2], pg->clip[3]);
}

static void
MD_SetContent(mpage_t *pg, mwidget_t *w, int h)
{
	w->h = h > w->minh ? h : w->minh;

	if (w->y + ML_TotalH(w) > pg->contenth)
	{
		pg->contenth = w->y + ML_TotalH(w);
	}
}

/* ------------------------------------------------------------------ */
/* server list (SPEC-803 section 4)                                     */

typedef struct
{
	netadr_t adr;
	char host[64], map[64], players[32], type[64], ping[16];
	int flags, pingms, numplayers, maxplayers;
	char *status;
} msrv_t;

static msrv_t *ms_srv;
static int ms_numsrv, ms_maxsrv;
static int ms_imported;            /* GameSpy entries handed over so far */
static serverlist_state_t ms_slstate = SL_IDLE;
static int ms_sortdir[6];

static void
MS_Summary(void)
{
	int i, p = 0, m = 0;

	for (i = 0; i < ms_numsrv; i++)
	{
		p += ms_srv[i].numplayers;
		m += ms_srv[i].maxplayers;
	}

	Cvar_Set("player_summary", va("%d/%d", p, m));
	Cvar_Set("server_summary", va("%d", ms_numsrv));
}

void
MS_ClearServers(void)
{
	int i;

	for (i = 0; i < ms_numsrv; i++)
	{
		if (ms_srv[i].status)
		{
			Z_Free(ms_srv[i].status);
		}
	}

	ms_numsrv = 0;
	ms_imported = 0;
	Cvar_Set("menu_server", "-1");
	Cvar_Set("player_summary", "0/0");
	Cvar_Set("server_summary", "0");
	Cvar_Set("locked_summary", "0");
}

/* host + map + players + game + game type + flags + ping text */
static void
MS_AddRecord(netadr_t adr, const char *text)
{
	char f[7][64];
	const char *s = text;
	int i, n;
	msrv_t *r = NULL;
	qboolean isnew = false;

	for (i = 0; i < 7; i++)
	{
		while (*s == ' ')
		{
			s++;
		}

		n = 0;

		while (*s && *s != '+')
		{
			if (n < 63)
			{
				f[i][n++] = *s;
			}

			s++;
		}

		f[i][n] = 0;

		if (*s == '+')
		{
			s++;
		}
	}

	for (i = 0; i < ms_numsrv; i++)
	{
		if (NET_CompareAdr(ms_srv[i].adr, adr))
		{
			r = &ms_srv[i];
			break;
		}
	}

	if (!r)
	{
		if (ms_numsrv == ms_maxsrv)
		{
			ms_maxsrv = ms_maxsrv ? ms_maxsrv * 2 : 64;
			ms_srv = realloc(ms_srv, ms_maxsrv * sizeof(msrv_t));
		}

		r = &ms_srv[ms_numsrv++];
		memset(r, 0, sizeof(*r));
		r->adr = adr;
		isnew = true;
	}

	Q_strlcpy(r->host, f[0], sizeof(r->host));
	Q_strlcpy(r->map, f[1], sizeof(r->map));
	Q_strlcpy(r->players, f[2], sizeof(r->players));
	Q_strlcpy(r->type, f[3][0] ? f[3] : f[4], sizeof(r->type));
	r->flags = atoi(f[5]);
	Q_strlcpy(r->ping, f[6], sizeof(r->ping));
	r->pingms = atoi(f[6]);
	r->numplayers = atoi(f[2]);

	{
		const char *a = strchr(f[2], '/'), *b = a ? strchr(a + 1, '/') : NULL;
		r->maxplayers = b ? atoi(b + 1) : 0;
	}

	(void)isnew;
	MS_Summary();
}

/* LAN "info" reply: SOF+... */
void
M_AddToServerList(netadr_t adr, char *info)
{
	char text[512];
	size_t l;

	if (!Q_strncasecmp(info, "SOF+", 4))
	{
		info += 4;
	}

	Q_strlcpy(text, info, sizeof(text));
	l = strlen(text);

	if (l && text[l - 1] == '\n')
	{
		text[l - 1] = 0;
	}

	Q_strlcat(text, "+local+", sizeof(text));
	MS_AddRecord(adr, text);
}

/* "ack <ms>" reply to the ping of serverstatus: the record's ping and ping text */
void
MS_ServerAck(netadr_t adr, int sent)
{
	int i;

	for (i = 0; i < ms_numsrv; i++)
	{
		if (NET_CompareAdr(ms_srv[i].adr, adr))
		{
			int ping = sent > 0 ? Sys_Milliseconds() - sent : 0;

			if (ping < 0)
			{
				ping = 0;
			}

			ms_srv[i].pingms = ping;
			Com_sprintf(ms_srv[i].ping, sizeof(ms_srv[i].ping), "%d", ping);
			return;
		}
	}
}

void
MS_ServerPrint(netadr_t adr, const char *text)
{
	int i;

	for (i = 0; i < ms_numsrv; i++)
	{
		if (NET_CompareAdr(ms_srv[i].adr, adr))
		{
			if (ms_srv[i].status)
			{
				Z_Free(ms_srv[i].status);
			}

			ms_srv[i].status = CopyString(text);
			Cvar_Set("serverinfo", text);
			return;
		}
	}
}

/* hand-off of the GameSpy list (SPEC-204 1.4) and browser status text */
void
MS_ServerFrame(void)
{
	serverlist_state_t st = CL_ServerList_State();
	int n = CL_ServerList_Count();

	if (n < ms_imported)
	{
		ms_imported = 0;
	}

	for (; ms_imported < n; ms_imported++)
	{
		const serverlist_entry_t *e = CL_ServerList_Entry(ms_imported);
		netadr_t adr;

		if (!e)
		{
			continue;
		}

		memset(&adr, 0, sizeof(adr));
		adr.type = NA_IP;
		memcpy(adr.ip, e->game.ip, 4);
		adr.port = BigShort((short)e->game.port);
		MS_AddRecord(adr, e->text);
	}

	/* OpenMerc: servers of a received list are queried while a slow master
	   still talks (up to SL_MASTER_MSEC); once rows arrive the status says so
	   instead of staying on the transfer text */
	if (st == SL_TRANSFERRING && n > 0)
	{
		st = SL_QUERYING;
	}

	if (st != ms_slstate)
	{
		if (st == SL_TRANSFERRING)
		{
			Cvar_Set("menu_servername", SL_StatusText("MENU_OMSET_SL_TRANSFER", "Transferring Server List"));
		}
		else if (st == SL_QUERYING)
		{
			Cvar_Set("menu_servername", SL_StatusText("MENU_OMSET_SL_QUERY", "Querying master servers"));
		}
		else
		{
			Cvar_Set("menu_servername", SL_StatusText("MENU_OMSET_SL_DONE", "Query Complete"));
		}

		ms_slstate = st;
	}
}

/* "pingservers" (client network code) starts: the browser list is cleared */
void
M_PingServers(void)
{
	MS_ClearServers();
}

/* serverstatus <server>: info, status and ping requests (SPEC-200 client query
   commands); the replies fill the browser record (SPEC-803 section 4). The
   client network code has no such command, so the menus provide it. */
static void
MS_ServerStatus_f(void)
{
	netadr_t adr;

	if (Cmd_Argc() < 2)
	{
		Com_Printf("usage: serverstatus <server>\n");
		return;
	}

	NET_Config(true);

	if (!NET_StringToAdr(Cmd_Argv(1), &adr))
	{
		Com_Printf("Bad address: %s\n", Cmd_Argv(1));
		return;
	}

	if (!adr.port)
	{
		adr.port = BigShort(PORT_SERVER);
	}

	Netchan_OutOfBandPrint(NS_CLIENT, adr, "info %i", 33);
	Netchan_OutOfBandPrint(NS_CLIENT, adr, "status\n");
	Netchan_OutOfBandPrint(NS_CLIENT, adr, "ping %i\n", Sys_Milliseconds());
}

void
MS_Init(void)
{
	Cvar_Get("player_summary", "0/0", 0);
	Cvar_Get("server_summary", "0", 0);
	Cvar_Get("locked_summary", "0", 0);

	if (!Cmd_Exists("serverstatus"))
	{
		Cmd_AddCommand("serverstatus", MS_ServerStatus_f);
	}
}

static const char *
MS_Field(const msrv_t *r, int f)
{
	switch (f)
	{
		case 0: return r->host;
		case 1: return r->map;
		case 2: return r->players;
		case 3: return r->type;
		case 4: return r->ping;
		default:
			return MSP_Find((r->flags & 32) ? "MENU_DM_GENERIC_PURE_YES" : "MENU_DM_GENERIC_PURE_NO");
	}
}

static int ms_sortfield, ms_sortsign;

static int
MS_Compare(const void *a, const void *b)
{
	const msrv_t *x = a, *y = b;
	int c = 0;

	switch (ms_sortfield)
	{
		case 0: c = Q_stricmp(x->host, y->host); break;
		case 1: c = Q_stricmp(x->map, y->map); break;
		case 3: c = Q_stricmp(x->type, y->type); break;
		case 2: c = x->numplayers - y->numplayers; break;
		case 4: c = x->pingms - y->pingms; break;
		default: c = 0; break;
	}

	return c * ms_sortsign;
}

/* ------------------------------------------------------------------ */
/* serverbox                                                            */

typedef struct
{
	int fields[16];
	char titles[16][64];
	int numcols;
	char hostname[64], address[64];
	int lastcount;
} serverbox_t;

static qboolean
SB_Option(mparse_t *p, mwidget_t *w, const char *kw)
{
	serverbox_t *d = w->data;
	char a[2][MENU_MAX_TOKEN];

	if (!Q_stricmp(kw, "column"))
	{
		MP_Args(p, a, 2);

		if (d->numcols < 16)
		{
			int f = atoi(a[0]);
			d->fields[d->numcols] = f < 0 ? 0 : (f > 5 ? 5 : f);
			Q_strlcpy(d->titles[d->numcols], a[1], sizeof(d->titles[0]));
			d->numcols++;
		}
	}
	else if (!Q_stricmp(kw, "hostname"))
	{
		MP_Args(p, a, 1);
		Q_strlcpy(d->hostname, a[0], sizeof(d->hostname));
	}
	else if (!Q_stricmp(kw, "address"))
	{
		MP_Args(p, a, 1);
		Q_strlcpy(d->address, a[0], sizeof(d->address));
	}
	else
	{
		return false;
	}

	return true;
}

static qboolean
SB_Finish(mparse_t *p, mwidget_t *w)
{
	serverbox_t *d = w->data;
	mpage_t *pg = p->page;

	w->w = (d->numcols < pg->numtabs) ? pg->tabs[d->numcols] : ML_ContentW(pg);

	if (w->w > ML_ContentW(pg))
	{
		w->w = ML_ContentW(pg);
	}

	if (w->minh < 200)
	{
		w->h = w->minh = 200;
	}

	w->flags |= WF_VARIABLE | WF_INTERACTIVE;
	return true;
}

static qboolean
SB_EndPage(mpage_t *pg, mwidget_t *w)
{
	(void)w;

	if (!pg->dry && ms_numsrv == 0)
	{
		M_QueueCommand("pingservers", true);
	}

	return true;
}

static void
SB_Update(mpage_t *pg, mwidget_t *w)
{
	serverbox_t *d = w->data;

	if (d->lastcount != ms_numsrv)
	{
		d->lastcount = ms_numsrv;
		MD_SetContent(pg, w, (ms_numsrv + 2) * MD_FH(w));
	}
}

static void
SB_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	serverbox_t *d = w->data;
	const char *cells[16];
	int L = MD_FH(w), i, j, hover;

	if (w->cvar && w->cvar->value == -1)
	{
		if (d->hostname[0])
		{
			Cvar_Set(d->hostname, Cvar_VariableString("menu_nss"));
		}

		if (d->address[0])
		{
			Cvar_Set(d->address, "");
		}

		Cvar_Set(w->cvar->name, "-2");
	}

	for (j = 0; j < d->numcols; j++)
	{
		cells[j] = d->titles[j];
	}

	MD_Columns(pg, w, x, y, cells, d->numcols, 0xFFFFFFFF);
	MR_Line((float)x, y + 1.5f * L, (float)(x + w->w), y + 1.5f * L, 1.0f, 0xFF808080);

	hover = (w->flags & WF_HOVER) ? MD_Row(pg, w, 2, NULL) : -1;

	for (i = 0; i < ms_numsrv; i++)
	{
		for (j = 0; j < d->numcols; j++)
		{
			cells[j] = MS_Field(&ms_srv[i], d->fields[j]);
		}

		MD_Columns(pg, w, x, y + (i + 2) * L, cells, d->numcols, i == hover ? w->atint : w->tint);
	}
}

static qboolean
SB_Key(mpage_t *pg, mwidget_t *w, int key)
{
	serverbox_t *d = w->data;
	int col, row = MD_Row(pg, w, 2, &col);

	if (row == -2 && key == K_MOUSE1)
	{
		int j, c = -1;

		for (j = 0; j < d->numcols; j++)
		{
			int x0 = j < pg->numtabs ? pg->tabs[j] : 0;
			int x1 = (j + 1 < pg->numtabs) ? pg->tabs[j + 1] : w->w;

			if (col >= x0 && col < x1)
			{
				c = j;
			}
		}

		if (c >= 0)
		{
			int f = d->fields[c];

			ms_sortdir[f] = ms_sortdir[f] > 0 ? -1 : 1;
			ms_sortfield = f;
			ms_sortsign = ms_sortdir[f];

			if (f != 5)
			{
				qsort(ms_srv, ms_numsrv, sizeof(msrv_t), MS_Compare);
			}

			if (w->cvar)
			{
				Cvar_Set(w->cvar->name, "-1");
			}

			M_PlaySound("menu/small/select.wav");
		}
	}
	else if (row >= 0 && row < ms_numsrv && (key == K_MOUSE1 || key == K_MOUSE2))
	{
		msrv_t *r = &ms_srv[row];

		if (d->hostname[0])
		{
			Cvar_Set(d->hostname, r->host);
		}

		if (d->address[0])
		{
			Cvar_Set(d->address, NET_AdrToString(r->adr));
		}

		M_QueueCommand(va("serverstatus %s", NET_AdrToString(r->adr)), true);

		if (w->cvar)
		{
			Cvar_SetValue(w->cvar->name, (float)row);
		}

		M_PlaySound("menu/small/select.wav");
	}

	return true;
}

static const mwclass_t md_serverbox = {
	"serverbox", SB_Option, SB_Finish, SB_EndPage, NULL, SB_Update, SB_Draw, SB_Key, NULL, NULL
};

/* ------------------------------------------------------------------ */
/* translate, serverdetail, players                                     */

static qboolean
Translate_Tag(mparse_t *p)
{
	char a[3][MENU_MAX_TOKEN], kw[MENU_MAX_TOKEN];
	int N, t;
	mtranslate_t **tail;

	MP_Args(p, a, 1);
	N = atoi(a[0]);

	for (tail = &p->screen->translates; *tail; tail = &(*tail)->next)
	{
	}

	while ((t = MP_Token(p, kw)) != 0)
	{
		mtranslate_t *e;

		if (t == 2)
		{
			if (kw[0] == '<')
			{
				MP_Unread(p);
			}

			return true;
		}

		if (!Q_stricmp(kw, "conv"))
		{
			MP_Args(p, a, 2);
			e = Z_Malloc(sizeof(*e));
			e->gametype = N;
			Q_strlcpy(e->key, a[0], sizeof(e->key));
			Q_strlcpy(e->label, a[1], sizeof(e->label));
		}
		else if (!Q_stricmp(kw, "convbits"))
		{
			MP_Args(p, a, 3);
			e = Z_Malloc(sizeof(*e));
			e->gametype = N;
			e->bits = true;
			Q_strlcpy(e->key, a[0], sizeof(e->key));
			Q_strlcpy(e->label, a[1], sizeof(e->label));
			Q_strlcpy(e->names, a[2], sizeof(e->names));
		}
		else
		{
			MP_Error(p, "Unrecognised translate keyword");
			return true;
		}

		*tail = e;
		tail = &e->next;
	}

	return true;
}

typedef struct
{
	char titles[8][64];
	int numtitles;
	int lastindex;
	char cells[256][2][128];
	int rows;
	int players[256];   /* players: frags, ping in cells, name in [0] */
	qboolean isplayers;
	char pcells[256][3][64];
} detail_t;

static qboolean
Detail_Option(mparse_t *p, mwidget_t *w, const char *kw)
{
	detail_t *d = w->data;
	char a[1][MENU_MAX_TOKEN];

	if (!Q_stricmp(kw, "title"))
	{
		char **items;
		int n, i;

		MP_Args(p, a, 1);
		MW_SplitComma(a[0], &items, &n);

		for (i = 0; i < n && i < 8; i++)
		{
			Q_strlcpy(d->titles[i], items[i], sizeof(d->titles[0]));
		}

		d->numtitles = i;
		MW_FreeList(items, n);
		return true;
	}

	return false;
}

static const msrv_t *
Detail_Server(mwidget_t *w)
{
	int i = w->cvar ? (int)w->cvar->value : -1;
	return (i >= 0 && i < ms_numsrv) ? &ms_srv[i] : NULL;
}

static void
Detail_Build(mpage_t *pg, mwidget_t *w)
{
	detail_t *d = w->data;
	const msrv_t *r = Detail_Server(w);
	const mtranslate_t *e;
	char info[2048], *nl;
	int dm;

	d->rows = 0;

	if (!r || !r->status)
	{
		return;
	}

	Q_strlcpy(info, r->status, sizeof(info));
	nl = strchr(info, '\n');

	if (nl)
	{
		*nl = 0;
	}

	dm = atoi(Info_ValueForKey(info, "deathmatch"));

	for (e = pg->screen->translates; e && d->rows < 256; e = e->next)
	{
		const char *v;

		if (e->gametype != 0 && e->gametype != dm)
		{
			continue;
		}

		v = Info_ValueForKey(info, e->key);

		if (!e->bits)
		{
			if (v[0])
			{
				Q_strlcpy(d->cells[d->rows][0], e->label, sizeof(d->cells[0][0]));
				Q_strlcpy(d->cells[d->rows][1], v, sizeof(d->cells[0][1]));
				d->rows++;
			}
		}
		else
		{
			char **names;
			int n, i, bits = atoi(v);
			qboolean first = true;

			MW_SplitComma(e->names, &names, &n);

			for (i = 0; i < n && i < 32 && d->rows < 256; i++)
			{
				if ((bits & (1 << i)) && names[i][0])
				{
					Q_strlcpy(d->cells[d->rows][0], first ? e->label : "", sizeof(d->cells[0][0]));
					Q_strlcpy(d->cells[d->rows][1], names[i], sizeof(d->cells[0][1]));
					d->rows++;
					first = false;
				}
			}

			MW_FreeList(names, n);
		}
	}
}

static void
Players_Build(mwidget_t *w)
{
	detail_t *d = w->data;
	const msrv_t *r = Detail_Server(w);
	char *s, *line;

	d->rows = 0;

	if (!r || !r->status)
	{
		return;
	}

	s = strchr(r->status, '\n');

	while (s && *s && d->rows < 256)
	{
		char buf[256], *p;
		const char *t;
		int n = 0;

		s++;
		line = s;

		while (*s && *s != '\n')
		{
			s++;
		}

		n = (int)(s - line);

		if (n > 255)
		{
			n = 255;
		}

		memcpy(buf, line, n);
		buf[n] = 0;
		p = buf;

		t = COM_Parse(&p);
		Q_strlcpy(d->pcells[d->rows][1], t, sizeof(d->pcells[0][1]));
		t = COM_Parse(&p);
		Q_strlcpy(d->pcells[d->rows][2], t, sizeof(d->pcells[0][2]));
		t = COM_Parse(&p);

		if (!t[0])
		{
			continue;
		}

		Q_strlcpy(d->pcells[d->rows][0], t, sizeof(d->pcells[0][0]));
		Q_strlcpy(d->pcells[d->rows][1], va("%d", atoi(d->pcells[d->rows][1])), sizeof(d->pcells[0][1]));
		Q_strlcpy(d->pcells[d->rows][2], va("%d", atoi(d->pcells[d->rows][2])), sizeof(d->pcells[0][2]));
		d->rows++;
	}
}

static void
Detail_Update(mpage_t *pg, mwidget_t *w)
{
	detail_t *d = w->data;
	int idx = w->cvar ? (int)w->cvar->value : -1;

	if (idx != d->lastindex || d->rows == 0)
	{
		d->lastindex = idx;

		if (d->isplayers)
		{
			Players_Build(w);
		}
		else
		{
			Detail_Build(pg, w);
		}

		MD_SetContent(pg, w, (d->rows + 2) * MD_FH(w));
	}
}

static void
Detail_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	detail_t *d = w->data;
	const char *cells[8];
	int L = MD_FH(w), i, j;
	mcolor_t c = (w->flags & WF_HOVER) ? w->atint : w->tint;

	/* black-box (original, join page): nothing, not even the titles, is
	   drawn while no listed server is selected */
	if (!Detail_Server(w))
	{
		return;
	}

	for (j = 0; j < d->numtitles; j++)
	{
		cells[j] = d->titles[j];
	}

	MD_Columns(pg, w, x, y, cells, d->numtitles, c);
	MR_Line((float)x, y + 1.5f * L, (float)(x + w->w), y + 1.5f * L, 1.0f, 0xFF808080);

	for (i = 0; i < d->rows; i++)
	{
		if (d->isplayers)
		{
			for (j = 0; j < 3; j++)
			{
				cells[j] = d->pcells[i][j];
			}

			MD_Columns(pg, w, x, y + (i + 2) * L, cells, 3, c);
		}
		else
		{
			cells[0] = d->cells[i][0];
			cells[1] = d->cells[i][1];
			MD_Columns(pg, w, x, y + (i + 2) * L, cells, 2, c);
		}
	}
}

static const mwclass_t md_serverdetail = {
	"serverdetail", Detail_Option, NULL, NULL, NULL, Detail_Update, Detail_Draw, NULL, NULL, NULL
};

static const mwclass_t md_players = {
	"players", Detail_Option, NULL, NULL, NULL, Detail_Update, Detail_Draw, NULL, NULL, NULL
};

/* ------------------------------------------------------------------ */
/* lobby chat: logs and widgets (section 3)                             */

#define CHAT_LOG 100

static char *mc_public[CHAT_LOG], *mc_private[CHAT_LOG];
static int mc_numpublic, mc_numprivate;
static char mc_selected[64];

static strlist_t mc_rooms, mc_users;

static void
MC_Add(char **log, int *num, const char *text)
{
	/* OpenMerc (I2d): the logs exist only while the menus do - in 1.07f lines
	   that arrive while the menus are closed are lost (black-box, I2d) */
	if (!ms.active)
	{
		return;
	}

	if (*num == CHAT_LOG)
	{
		Z_Free(log[CHAT_LOG - 1]);
		(*num)--;
	}

	memmove(log + 1, log, *num * sizeof(char *));
	log[0] = CopyString(text);
	(*num)++;
}

/* entry points for the GameSpy chat client (gsc_* commands) */
void
MC_PublicLine(const char *text)
{
	MC_Add(mc_public, &mc_numpublic, text);
}

void
MC_PrivateLine(const char *text)
{
	MC_Add(mc_private, &mc_numprivate, text);
}

/* OpenMerc (I2d): the user picked in the users widget (private messages from
   him go to the private log, SPEC-204 5.5) */
const char *
MC_SelectedUser(void)
{
	return mc_selected;
}

static int
MC_SortCmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* OpenMerc (I2d): the room and user lists are changed one operation at a time
   by the chat client; like the logs they exist only while the menus do, so
   the operations are ignored while no menu is open (SPEC-AMEND-006 I2d.7).
   Both lists stay sorted; duplicates are kept. */
void
MC_ClearRooms(void)
{
	if (ms.active)
	{
		StrList_Free(&mc_rooms);
		StrList_Init(&mc_rooms, 0);
	}
}

void
MC_AddRoom(const char *room)
{
	if (ms.active)
	{
		StrList_Append(&mc_rooms, room);
		qsort(mc_rooms.data, mc_rooms.num, sizeof(char *), MC_SortCmp);
	}
}

void
MC_ClearUsers(void)
{
	if (ms.active)
	{
		StrList_Free(&mc_users);
		StrList_Init(&mc_users, 0);
	}
}

void
MC_AddUser(const char *entry)
{
	if (ms.active)
	{
		StrList_Append(&mc_users, entry);
		qsort(mc_users.data, mc_users.num, sizeof(char *), MC_SortCmp);
	}
}

/* OpenMerc (I2d): menu.c calls this when the last menu closes: the logs, the
   room and user lists and the selected user end with the menu system
   (SPEC-AMEND-006 I2d.7) */
void
MC_ClearLobby(void)
{
	int i;

	for (i = 0; i < mc_numpublic; i++)
	{
		Z_Free(mc_public[i]);
	}

	for (i = 0; i < mc_numprivate; i++)
	{
		Z_Free(mc_private[i]);
	}

	mc_numpublic = mc_numprivate = 0;
	StrList_Free(&mc_rooms);
	StrList_Init(&mc_rooms, 0);
	StrList_Free(&mc_users);
	StrList_Init(&mc_users, 0);
	mc_selected[0] = 0;
}

typedef struct
{
	qboolean isprivate;
	int lastlines;
	int lastcount;
	int h0;      /* OpenMerc (I2d): the height from the frame layout */
} chat_t;

static qboolean
Chat_Option(mparse_t *p, mwidget_t *w, const char *kw)
{
	chat_t *d = w->data;

	(void)p;

	if (!Q_stricmp(kw, "private"))
	{
		d->isprivate = true;
		return true;
	}

	return false;
}

static qboolean
MD_RestOfFrame(mparse_t *p, mwidget_t *w)
{
	mpage_t *pg = p->page;

	/* inner width minus the scroll bar, inner height */
	if (w->w <= 0)
	{
		w->w = ML_ContentW(pg);
	}

	if (ML_ContentH(pg) > w->minh)
	{
		w->h = w->minh = ML_ContentH(pg);
	}

	w->flags |= WF_VARIABLE;
	return true;
}

static qboolean
Chat_Finish(mparse_t *p, mwidget_t *w)
{
	chat_t *d = w->data;

	if (p->page->chats++)
	{
		Com_Printf("Error : Only one chat allowed per page\n");
		return false;
	}

	if (d->isprivate && !p->dry)
	{
		int i;

		for (i = 0; i < mc_numprivate; i++)
		{
			Z_Free(mc_private[i]);
		}

		mc_numprivate = 0;
		MC_PrivateLine(va("\x04Started private chat with %s", mc_selected));
	}

	return MD_RestOfFrame(p, w);
}

/* word wrap into lines; returns the number of lines, fills out if given */
static int
Chat_Wrap(mwidget_t *w, const char *msg, char lines[][256], int max)
{
	char line[256];
	int n = 0, lw = 0, space = MR_StringWidth(" ", w->font);
	const char *s = msg;

	line[0] = 0;

	while (*s)
	{
		char word[256];
		int wl = 0, ww;

		while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
		{
			s++;
		}

		if (!*s)
		{
			break;
		}

		while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n' && wl < 255)
		{
			word[wl++] = *s++;
		}

		word[wl] = 0;
		ww = MR_StringWidth(word, w->font);

		if (lw + ww + space < w->w || (!line[0] && lw == 0 && ww + space < w->w))
		{
			if (line[0])
			{
				Q_strlcat(line, " ", sizeof(line));
			}

			Q_strlcat(line, word, sizeof(line));
			lw += ww + space;
		}
		else
		{
			if (lines && n < max)
			{
				Q_strlcpy(lines[n], line, 256);
			}

			n++;
			Q_strlcpy(line, word, sizeof(line));
			lw = ww + 2 * space;
		}
	}

	if (lines && n < max)
	{
		Q_strlcpy(lines[n], line, 256);
	}

	return n + 1;
}

static void
Chat_Update(mpage_t *pg, mwidget_t *w)
{
	chat_t *d = w->data;
	char **log = d->isprivate ? mc_private : mc_public;
	int num = d->isprivate ? mc_numprivate : mc_numpublic;
	int i, lines = 0;

	for (i = 0; i < num; i++)
	{
		lines += Chat_Wrap(w, log[i], NULL, 0);
	}

	if (!d->h0)
	{
		d->h0 = w->h;
	}

	/* OpenMerc (I2d): SPEC-AMEND-006 I2d.8 - once the text is at least as tall
	   as the widget, the widget's own height follows it (N lines * L), the
	   page's content height is taken from its widgets again, and a page with
	   a vertical bar scrolls to the bottom; the drawing (from the widget's
	   scrolled y + its current height - 4) then keeps the newest line at the
	   visible bottom. Below that the widget keeps its height and nothing
	   changes. */
	if (lines != d->lastlines)
	{
		int L = MD_FH(w);

		d->lastlines = lines;

		if (lines * L >= d->h0)
		{
			int j;

			w->h = lines * L;
			pg->contenth = 0;

			for (j = 0; j < pg->numwidgets; j++)
			{
				const mwidget_t *o = pg->widgets[j];

				if (!(o->flags & (WF_NOTPLACED | WF_BOLTED)) &&
					(o->y + ML_TotalH(o) > pg->contenth))
				{
					pg->contenth = o->y + ML_TotalH(o);
				}
			}

			if ((pg->contenth > ML_ContentH(pg)) && pg->vbar)
			{
				pg->scroll = pg->contenth - ML_ContentH(pg);
			}
		}
	}
}

static void
Chat_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	chat_t *d = w->data;
	char **log = d->isprivate ? mc_private : mc_public;
	int num = d->isprivate ? mc_numprivate : mc_numpublic;
	int L = MD_FH(w), i, by = y + w->h - 4;
	static char lines[64][256];

	MR_ClipRight(pg->frame->fx + pg->frame->pad + ML_ContentW(pg) + pg->screen->originx);

	for (i = 0; i < num; i++)
	{
		int n = Chat_Wrap(w, log[i], lines, 64), j;

		by -= n * L;

		if (by + n * L < y)
		{
			break;
		}

		for (j = 0; j < n && j < 64; j++)
		{
			MR_DrawString(x, by + j * L, lines[j], 0xFFFFFFFF, w->font, 1);
		}
	}

	MR_Reclip();
}

static const mwclass_t md_chat = {
	"chat", Chat_Option, Chat_Finish, NULL, NULL, Chat_Update, Chat_Draw, NULL, NULL, NULL
};

typedef struct
{
	qboolean rooms;
	int lastcount;
} roster_t;

static qboolean
Roster_Finish(mparse_t *p, mwidget_t *w)
{
	w->flags |= WF_INTERACTIVE;
	return MD_RestOfFrame(p, w);
}

static void
Roster_Update(mpage_t *pg, mwidget_t *w)
{
	roster_t *d = w->data;
	int n = d->rooms ? mc_rooms.num : mc_users.num;

	if (n != d->lastcount)
	{
		d->lastcount = n;
		pg->contenth = n * MD_FH(w);
	}
}

static void
Roster_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	roster_t *d = w->data;
	strlist_t *l = d->rooms ? &mc_rooms : &mc_users;
	int i, L = MD_FH(w);
	int hover = (d->rooms && (w->flags & WF_HOVER)) ? MD_Row(pg, w, 0, NULL) : -1;

	for (i = 0; i < l->num; i++)
	{
		MR_DrawString(x, y + i * L, l->data[i], i == hover ? w->atint : w->tint, w->font, 0);
	}
}

static qboolean
Roster_Key(mpage_t *pg, mwidget_t *w, int key)
{
	roster_t *d = w->data;
	strlist_t *l = d->rooms ? &mc_rooms : &mc_users;
	int row = MD_Row(pg, w, 0, NULL);

	if (key == K_MOUSE1 && row >= 0 && row < l->num)
	{
		const char *e = l->data[row];

		if (d->rooms)
		{
			if (w->cvar)
			{
				Cvar_Set(w->cvar->name, e);
			}

			return false;
		}

		if (strlen(e) >= 2)
		{
			Q_strlcpy(mc_selected, e + 2, sizeof(mc_selected));

			if (w->cvar)
			{
				Cvar_Set(w->cvar->name, e + 2);
			}
		}
	}

	return !d->rooms;
}

static const mwclass_t md_users = {
	"users", NULL, Roster_Finish, NULL, NULL, Roster_Update, Roster_Draw, Roster_Key, NULL, NULL
};

static const mwclass_t md_rooms = {
	"rooms", NULL, Roster_Finish, NULL, NULL, Roster_Update, Roster_Draw, Roster_Key, NULL, NULL
};

/* ------------------------------------------------------------------ */
/* filebox / filereq (section 5)                                        */

typedef struct
{
	char dir[MAX_QPATH], pattern[MAX_QPATH], ext[32];
	mcolor_t backfill;
	qboolean hasbackfill;
	char sort[16];
	strlist_t names;
	qboolean req;
	char subdir[MAX_QPATH];
	int numdirs;
} filebox_t;

static int fb_sortsign;

static int
FB_Cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b) * fb_sortsign;
}

static void
FB_List(mwidget_t *w)
{
	filebox_t *d = w->data;
	char find[MAX_QPATH * 2];
	strlist_t files, paks;
	int i;
	qboolean strip = !strchr(d->ext, '*') && d->ext[0];
	size_t dl = strlen(d->dir);

	StrList_Free(&d->names);
	StrList_Init(&d->names, 0);

	Com_sprintf(find, sizeof(find), "%s/%s.%s", d->dir, d->pattern, d->ext);
	files = FS_ListFiles2(find, 0, SFF_SUBDIR);
	paks = FS_ListFiles2(find, SFF_INPACK, SFF_SUBDIR);

	for (i = 0; i < files.num; i++)
	{
		char name[MAX_QPATH];
		const char *s = files.data[i];

		if (!Q_strncasecmp(s, d->dir, (int)dl) && s[dl] == '/')
		{
			s += dl + 1;
		}

		Q_strlcpy(name, s, sizeof(name));
		MW_ListedCase(name, files.data[i], &paks); /* SPEC-AMEND-005 I6m.3 */

		if (strip)
		{
			size_t l = strlen(name), el = strlen(d->ext);

			if (l > el + 1 && !Q_stricmp(name + l - el, d->ext) && name[l - el - 1] == '.')
			{
				name[l - el - 1] = 0;
			}
		}

		StrList_Append(&d->names, name);
	}

	StrList_Free(&files);
	StrList_Free(&paks);

	if (!Q_stricmp(d->sort, "name") || !Q_stricmp(d->sort, "rname"))
	{
		fb_sortsign = Q_stricmp(d->sort, "rname") ? 1 : -1;
		qsort(d->names.data, d->names.num, sizeof(char *), FB_Cmp);
	}
}

static qboolean
FB_Option(mparse_t *p, mwidget_t *w, const char *kw)
{
	filebox_t *d = w->data;
	char a[1][MENU_MAX_TOKEN];

	if (!Q_stricmp(kw, "backfill"))
	{
		MP_Args(p, a, 1);
		d->backfill = MP_Color(p->screen, a[0]);
		d->hasbackfill = true;
	}
	else if (!Q_stricmp(kw, "sort"))
	{
		MP_Args(p, a, 1);
		Q_strlcpy(d->sort, a[0], sizeof(d->sort));
	}
	else if (!Q_stricmp(kw, "talign"))
	{
		MP_Args(p, a, 1);

		if (Q_stricmp(a[0], "left") && Q_stricmp(a[0], "right") && Q_stricmp(a[0], "center"))
		{
			MP_Error(p, "Invalid align parameter");
		}
	}
	else
	{
		return false;
	}

	return true;
}

static qboolean
FB_Finish(mparse_t *p, mwidget_t *w)
{
	filebox_t *d = w->data;

	if (!d->hasbackfill)
	{
		d->backfill = p->page->backdrop ? p->page->bgcolor : 0;
	}

	if (!p->dry)
	{
		FB_List(w);
	}

	if (w->w <= 0)
	{
		w->w = 16 * MD_FH(w);
	}

	if (d->names.num * MD_FH(w) > w->minh)
	{
		w->h = w->minh = d->names.num * MD_FH(w);
	}

	if (!d->req)
	{
		w->flags |= WF_VARIABLE;
	}

	w->flags |= WF_INTERACTIVE;
	return true;
}

static void
FB_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	filebox_t *d = w->data;
	int i, L = MD_FH(w);
	int hover = (w->flags & WF_HOVER) ? MD_Row(pg, w, 0, NULL) : -1;

	MR_Fill(x, y, w->w, w->h, d->backfill);

	for (i = 0; i < d->names.num; i++)
	{
		const char *s = d->names.data[i];
		int tx = x, sw = MR_StringWidth(s, w->font);

		if (w->align == MA_CENTER)
		{
			tx = x + (w->w - sw) / 2;
		}
		else if (w->align == MA_RIGHT)
		{
			tx = x + w->w - sw;
		}

		if (i == hover)
		{
			MR_Fill(x, y + i * L, w->w, L, w->btint);
		}

		MR_DrawString(tx, y + i * L, s, i == hover ? w->atint : w->tint, w->font, 0);
	}
}

static qboolean
FB_Key(mpage_t *pg, mwidget_t *w, int key)
{
	filebox_t *d = w->data;
	int row = MD_Row(pg, w, 0, NULL);

	if (key == K_MOUSE1 && row >= 0 && row < d->names.num)
	{
		if (w->cvar)
		{
			Cvar_Set(w->cvar->name, d->names.data[row]);
		}

		M_PlaySound("menu/small/select.wav");
	}

	return true;
}

static void
FB_Free(mwidget_t *w)
{
	filebox_t *d = w->data;

	if (d)
	{
		StrList_Free(&d->names);
		Z_Free(d);
	}
}

static const mwclass_t md_filebox = {
	"filebox", FB_Option, FB_Finish, NULL, NULL, NULL, FB_Draw, FB_Key, NULL, FB_Free
};

static const mwclass_t md_filereq = {
	"filereq", FB_Option, FB_Finish, NULL, NULL, NULL, FB_Draw, FB_Key, NULL, FB_Free
};

/* ------------------------------------------------------------------ */
/* loadbox (savegames)                                                   */

typedef struct
{
	char name[64];
	char level[64], time[64], skill[32];
	long mtime;
} msave_t;

typedef struct
{
	mcolor_t backfill;
	qboolean hasbackfill;
	char sort[16];
	int spacing;
	qboolean saving;
	msave_t *saves;
	int numsaves;
	int c, k, m, r;
} loadbox_t;

static int lb_sortsign;

/* MENU2 (docs/DEVIATIONS.md "Savegame date labels"): the savegame's "Www Mmm dd hh:mm:ss yyyy" time
   is shown as "yyyy-mm-dd hh:mm", so it fits its cell instead of running into the neighbouring save */
static void
LB_ShortTime(char *time, size_t size)
{
	static const char *months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	char wd[8], mon[8];
	int day, hh, mm, ss, year, i;

	if (sscanf(time, "%7s %7s %d %d:%d:%d %d", wd, mon, &day, &hh, &mm, &ss, &year) != 7)
	{
		return;
	}

	for (i = 0; i < 12; i++)
	{
		if (!Q_stricmp(mon, months[i]))
		{
			Com_sprintf(time, (int)size, "%04d-%02d-%02d %02d:%02d", year, i + 1, day, hh, mm);
			return;
		}
	}
}

static int
LB_CmpTime(const void *a, const void *b)
{
	const msave_t *x = a, *y = b;
	return (x->mtime < y->mtime ? -1 : x->mtime > y->mtime ? 1 : 0) * lb_sortsign;
}

static int
LB_CmpName(const void *a, const void *b)
{
	const msave_t *x = a, *y = b;
	return strcmp(x->name, y->name) * lb_sortsign;
}

/* MENU-SCRATCH (owner t96): the newest savegame for the landing page and
   "om_continue" outside a level; "current" (the running game) is skipped */
qboolean
M_NewestSave(char *name, size_t namesize, char *level, size_t levelsize,
		char *time, size_t timesize)
{
	char find[MAX_OSPATH], best[MAX_QPATH];
	strlist_t files;
	long bestt = -1;
	int i;
	static byte shot[65536];
	char comm[128], *x, *y;

	best[0] = 0;
	Com_sprintf(find, sizeof(find), "%s/save/*.sof", FS_Userdir());
	files = FS_ListFiles(find, 0, SFF_SUBDIR);

	for (i = 0; i < files.num; i++)
	{
		const char *path = files.data[i];
		const char *base = strrchr(path, '/');
		char nm[MAX_QPATH], *dot;
		struct stat st;

		base = base ? base + 1 : path;
		Q_strlcpy(nm, base, sizeof(nm));
		dot = strrchr(nm, '.');

		if (dot)
		{
			*dot = 0;
		}

		if (!Q_stricmp(nm, "current") || stat(path, &st) != 0)
		{
			continue;
		}

		if ((long)st.st_mtime > bestt)
		{
			bestt = (long)st.st_mtime;
			Q_strlcpy(best, nm, sizeof(best));
		}
	}

	StrList_Free(&files);

	if (!best[0] || !SG_ReadInfo(best, comm, sizeof(comm), shot))
	{
		return false;
	}

	Q_strlcpy(name, best, namesize);
	x = strchr(comm, '/');
	y = x ? strchr(x + 1, '/') : NULL;
	time[0] = 0;

	if (x && y)
	{
		*x = 0;
		*y = 0;
		Q_strlcpy(time, comm, timesize);
		Q_strlcpy(level, y + 1, levelsize);
	}
	else
	{
		Q_strlcpy(level, comm, levelsize);
	}

	/* the file time: autosaves write "ENTERING" instead of a date */
	{
		time_t t = (time_t)bestt;
		struct tm *lt = localtime(&t);

		if (lt)
		{
			strftime(time, timesize, "%Y-%m-%d %H:%M", lt);
		}
	}

	MR_RawPic("pics/menus/om/newest", 128, 128, shot);
	return true;
}

static void
LB_List(mpage_t *pg, mwidget_t *w)
{
	loadbox_t *d = w->data;
	char find[MAX_OSPATH];
	strlist_t files;
	int i;

	free(d->saves);
	d->saves = NULL;
	d->numsaves = 0;

	Com_sprintf(find, sizeof(find), "%s/save/*.sof", FS_Userdir());
	files = FS_ListFiles(find, 0, SFF_SUBDIR);

	if (files.num > 0)
	{
		d->saves = calloc(files.num, sizeof(msave_t));
	}

	for (i = 0; i < files.num; i++)
	{
		const char *path = files.data[i];
		const char *base = strrchr(path, '/');
		static byte shot[65536];
		char picname[MAX_QPATH];
		msave_t *s = &d->saves[d->numsaves];
		char *dot;

		base = base ? base + 1 : path;
		Q_strlcpy(s->name, base, sizeof(s->name));
		dot = strrchr(s->name, '.');

		if (dot)
		{
			*dot = 0;
		}

		/* OpenMerc: the timed autosave slots are not offered for saving */
		if (!Q_stricmp(s->name, "current") ||
			(d->saving && (!Q_stricmp(s->name, "auto") || SG_IsTimedSlot(s->name))))
		{
			continue;
		}

		/* COMM and SHOT through the savegame container (I1a, SPEC-303) */
		{
			char comm[128], *x, *y;

			if (!SG_ReadInfo(s->name, comm, sizeof(comm), shot))
			{
				continue; /* saves that cannot be opened are dropped */
			}

			x = strchr(comm, '/');
			y = x ? strchr(x + 1, '/') : NULL;

			if (x && y)
			{
				*x = 0;
				*y = 0;
				Q_strlcpy(s->time, comm, sizeof(s->time));
				LB_ShortTime(s->time, sizeof(s->time));
				Q_strlcpy(s->skill, x + 1, sizeof(s->skill));
				Q_strlcpy(s->level, y + 1, sizeof(s->level));
			}
			else
			{
				Q_strlcpy(s->level, comm, sizeof(s->level));
			}

			/* OpenMerc: a timed autosave says so in its third line */
			if (SG_IsTimedSlot(s->name))
			{
				Q_strlcpy(s->skill, SG_AutosaveLabel(), sizeof(s->skill));
			}

			Com_sprintf(picname, sizeof(picname), "save/%s", s->name);
			MR_RawPic(picname, 128, 128, shot);
		}

		{
			struct stat st;

			if (stat(path, &st) == 0)
			{
				s->mtime = (long)st.st_mtime;
			}
		}

		d->numsaves++;
	}

	StrList_Free(&files);

	if (!d->sort[0] || !Q_stricmp(d->sort, "rtime") || !Q_stricmp(d->sort, "time"))
	{
		lb_sortsign = (!d->sort[0] || !Q_stricmp(d->sort, "rtime")) ? -1 : 1;
		qsort(d->saves, d->numsaves, sizeof(msave_t), LB_CmpTime);
	}
	else if (!Q_stricmp(d->sort, "name") || !Q_stricmp(d->sort, "rname"))
	{
		lb_sortsign = Q_stricmp(d->sort, "rname") ? 1 : -1;
		qsort(d->saves, d->numsaves, sizeof(msave_t), LB_CmpName);
	}

	/* grid (SPEC-803 section 5) */
	{
		int L = MD_FH(w);

		d->c = d->spacing + 128;
		d->k = w->w / d->c;

		if (d->k < 1)
		{
			d->k = 1;
		}

		d->m = (w->w - d->c * d->k) / (d->k + 1);
		d->r = 3 * L + 96 + d->spacing;
		w->h = w->minh = ((d->numsaves + d->k - 1) / d->k) * d->r;

		if (pg && w->y + w->h > pg->contenth)
		{
			pg->contenth = w->y + w->h;
		}
	}
}

static qboolean
LB_Option(mparse_t *p, mwidget_t *w, const char *kw)
{
	loadbox_t *d = w->data;
	char a[1][MENU_MAX_TOKEN];

	if (!Q_stricmp(kw, "backfill"))
	{
		MP_Args(p, a, 1);
		d->backfill = MP_Color(p->screen, a[0]);
		d->hasbackfill = true;
	}
	else if (!Q_stricmp(kw, "sort"))
	{
		MP_Args(p, a, 1);
		Q_strlcpy(d->sort, a[0], sizeof(d->sort));
	}
	else if (!Q_stricmp(kw, "spacing"))
	{
		MP_Args(p, a, 1);
		d->spacing = atoi(a[0]);
	}
	else if (!Q_stricmp(kw, "saving"))
	{
		d->saving = true;
	}
	else
	{
		return false;
	}

	return true;
}

static qboolean
LB_Finish(mparse_t *p, mwidget_t *w)
{
	loadbox_t *d = w->data;

	if (!d->hasbackfill)
	{
		d->backfill = p->page->backdrop ? p->page->bgcolor : 0;
	}

	/* width default 128, multiplied by s at the end of the tag */
	if (w->w <= 0)
	{
		w->w = 128;
	}

	w->w = (int)(w->w * p->screen->scale);

	if (!p->dry)
	{
		char path[MAX_OSPATH];

		/* the staging save "current" is wiped when the widget is created */
		Com_sprintf(path, sizeof(path), "%s/save/current.sof", FS_Userdir());
		Sys_Remove(path);

		if (d->saving && (Cvar_VariableValue("deathmatch") || Com_ServerState() == 0))
		{
			/* saving not possible: the whole page fails */
			p->page->failed = true;
		}

		LB_List(NULL, w);
	}

	w->flags |= WF_INTERACTIVE;
	return true;
}

static int
LB_Cell(mpage_t *pg, mwidget_t *w)
{
	loadbox_t *d = w->data;
	int x, y, mx, my, col, row, i;

	float fx, fy;

	ML_WidgetScreenPos(pg, w, &x, &y);
	ML_PageMouse(pg, w, &fx, &fy);
	mx = (int)fx - x - (d->m + d->spacing) / 2;
	my = (int)fy - y - d->spacing / 2;

	if (mx < 0 || my < 0 || d->c + d->m <= 0 || d->r <= 0)
	{
		return -1;
	}

	col = mx / (d->c + d->m);
	row = my / d->r;

	if (col >= d->k)
	{
		return -1;
	}

	i = row * d->k + col;
	return (i < d->numsaves) ? i : -1;
}

static void
LB_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	loadbox_t *d = w->data;
	int i, L = MD_FH(w);
	int hover = (w->flags & WF_HOVER) ? LB_Cell(pg, w) : -1;

	MR_Fill(x, y, w->w, w->h, d->backfill);

	for (i = 0; i < d->numsaves; i++)
	{
		int col = i % d->k, row = i / d->k;
		int cx = x + d->spacing + d->m + col * (d->c + d->m);
		int cy = y + d->spacing + row * d->r;
		mcolor_t c = (i == hover) ? w->atint : w->tint;
		const msave_t *s = &d->saves[i];
		int center = cx + 64;

		/* SPEC-803 open question 4: last argument 6 (passed through) */
		MR_StretchPic(cx, cy, 128, 96, 0xFFFFFFFF, va("save/%s", s->name), 6);
		MR_DrawString(center - MR_StringWidth(s->level, w->font) / 2, cy + 96, s->level, c, w->font, 0);
		MR_DrawString(center - MR_StringWidth(s->time, w->font) / 2, cy + 96 + L, s->time, c, w->font, 0);
		MR_DrawString(center - MR_StringWidth(s->skill, w->font) / 2, cy + 96 + 2 * L, s->skill, c, w->font, 0);
	}
}

static qboolean
LB_Key(mpage_t *pg, mwidget_t *w, int key)
{
	loadbox_t *d = w->data;
	int i = LB_Cell(pg, w);

	if ((key == K_MOUSE1 || key == K_MOUSE2) && i >= 0)
	{
		if (w->cvar)
		{
			Cvar_Set(w->cvar->name, d->saves[i].name);
		}

		M_PlaySound("menu/small/select.wav");

		if (key == K_MOUSE2 && !Q_stricmp(d->saves[i].name, "auto"))
		{
			M_PlaySound("menu/small/invalid.wav");
			return false;
		}
	}

	return true;
}

static void
LB_Refresh(mpage_t *pg, mwidget_t *w)
{
	LB_List(pg, w);
}

static void
LB_Free(mwidget_t *w)
{
	loadbox_t *d = w->data;

	if (d)
	{
		free(d->saves);
		Z_Free(d);
	}
}

static const mwclass_t md_loadbox = {
	"loadbox", LB_Option, LB_Finish, NULL, LB_Refresh, NULL, LB_Draw, LB_Key, NULL, LB_Free
};

/* ------------------------------------------------------------------ */
/* listfile, botlist, bot (section 6)                                   */

typedef struct
{
	cvar_t *listcvar;
	char loaded[MAX_QPATH];
	strlist_t maps, descs;
	qboolean bots;
} mlist_t;

static void
LF_Load(mpage_t *pg, mwidget_t *w)
{
	mlist_t *d = w->data;
	char path[MAX_QPATH * 2], *text, *s;
	int len;

	StrList_Free(&d->maps);
	StrList_Free(&d->descs);
	StrList_Init(&d->maps, 0);
	StrList_Init(&d->descs, 0);

	if (d->bots)
	{
		Q_strlcpy(path, "bot/botlist.txt", sizeof(path));
	}
	else
	{
		Q_strlcpy(d->loaded, d->listcvar ? d->listcvar->string : "", sizeof(d->loaded));
		Com_sprintf(path, sizeof(path), "maplists/%s.lst", d->loaded);
	}

	if (MP_ReadFile(path, &text, &len))
	{
		s = text;

		for (;;)
		{
			const char *t = COM_Parse(&s);

			if (!s && !t[0])
			{
				break;
			}

			if (!t[0])
			{
				if (!s)
				{
					break;
				}

				continue;
			}

			StrList_Append(&d->maps, t);

			if (!d->bots)
			{
				t = COM_Parse(&s);
				StrList_Append(&d->descs, t);
			}

			if (!s)
			{
				break;
			}
		}

		free(text);
	}

	MD_SetContent(pg, w, d->maps.num * MD_FH(w));
}

static qboolean
LF_Finish(mparse_t *p, mwidget_t *w)
{
	mlist_t *d = w->data;

	if (w->w <= 0)
	{
		w->w = MD_FrameW(p->page);
	}

	if (!p->dry)
	{
		LF_Load(p->page, w);

		if (!d->bots && w->cvar)
		{
			Cvar_Set(w->cvar->name, d->maps.num ? d->maps.data[0] : "dm/nycdm1");
		}
	}

	w->flags |= WF_VARIABLE | WF_INTERACTIVE;
	return true;
}

static void
LF_Update(mpage_t *pg, mwidget_t *w)
{
	mlist_t *d = w->data;

	if (!d->bots && d->listcvar && strcmp(d->listcvar->string, d->loaded))
	{
		LF_Load(pg, w);
	}
}

static void
LF_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	mlist_t *d = w->data;
	int i, L = MD_FH(w);
	int hover = (w->flags & WF_HOVER) ? MD_Row(pg, w, 0, NULL) : -1;

	for (i = 0; i < d->maps.num; i++)
	{
		const char *s = d->bots ? d->maps.data[i] : d->descs.data[i];
		int tx = x, sw = MR_StringWidth(s, w->font);

		if (w->align == MA_CENTER)
		{
			tx = x + (w->w - sw) / 2;
		}
		else if (w->align == MA_RIGHT)
		{
			tx = x + w->w - sw;
		}

		MR_DrawString(tx, y + i * L, s, i == hover ? w->atint : w->tint, w->font, 0);
	}
}

static qboolean
LF_Key(mpage_t *pg, mwidget_t *w, int key)
{
	mlist_t *d = w->data;
	int row = MD_Row(pg, w, 0, NULL);

	if (key == K_MOUSE1 && row >= 0 && row < d->maps.num && w->cvar)
	{
		Cvar_Set(w->cvar->name, d->maps.data[row]);
	}

	return false;
}

static void
LF_Free(mwidget_t *w)
{
	mlist_t *d = w->data;

	if (d)
	{
		StrList_Free(&d->maps);
		StrList_Free(&d->descs);
		Z_Free(d);
	}
}

static const mwclass_t md_listfile = {
	"listfile", NULL, LF_Finish, NULL, NULL, LF_Update, LF_Draw, LF_Key, NULL, LF_Free
};

static const mwclass_t md_botlist = {
	"botlist", NULL, LF_Finish, NULL, NULL, LF_Update, LF_Draw, LF_Key, NULL, LF_Free
};

typedef struct
{
	cvar_t *namecvar;
	char loaded[64];
	char headings[7][64];
	int numheadings;
	char values[7][128];
	char portrait[MAX_QPATH];
	qboolean read;
} bot_t;

static void
Bot_Load(bot_t *d)
{
	static const char *keys[7] = {
		"userinfo", "accuracy", "fov", "turning_speed", "view_distance", "general_intelligence", "canchat"
	};
	char path[MAX_QPATH * 2], *text, *s;
	int len, i;

	Q_strlcpy(d->loaded, d->namecvar ? d->namecvar->string : "", sizeof(d->loaded));
	memset(d->values, 0, sizeof(d->values));
	d->portrait[0] = 0;
	d->read = false;
	Com_sprintf(path, sizeof(path), "bot/bots/%s.bpf", d->loaded);

	if (!MP_ReadFile(path, &text, &len))
	{
		return;
	}

	d->read = true;
	s = text;

	while (*s)
	{
		char line[512];
		int n = 0;
		char *v;

		while (*s == ' ' || *s == '\t')
		{
			s++;
		}

		while (*s && *s != '\n' && *s != '\r' && n < 511)
		{
			line[n++] = *s++;
		}

		line[n] = 0;

		while (*s == '\n' || *s == '\r')
		{
			s++;
		}

		if (line[0] == '#' || !(v = strchr(line, '=')))
		{
			continue;
		}

		v++;

		for (i = 0; i < 7; i++)
		{
			if (!Q_strncasecmp(line, keys[i], (int)strlen(keys[i])))
			{
				if (i == 0)
				{
					char info[512];

					Com_sprintf(info, sizeof(info), "\\%s", v);
					Q_strlcpy(d->values[0], Info_ValueForKey(info, "name"), sizeof(d->values[0]));
					Com_sprintf(d->portrait, sizeof(d->portrait), "ghoul/pmodels/portraits/%s",
							Info_ValueForKey(info, "skin"));
				}
				else
				{
					Q_strlcpy(d->values[i], v, sizeof(d->values[i]));
				}

				break;
			}
		}
	}

	free(text);
}

static qboolean
Bot_Option(mparse_t *p, mwidget_t *w, const char *kw)
{
	bot_t *d = w->data;
	char a[1][MENU_MAX_TOKEN];

	if (!Q_stricmp(kw, "headings"))
	{
		char **items;
		int n, i;

		MP_Args(p, a, 1);
		MW_SplitComma(a[0], &items, &n);

		for (i = 0; i < n && i < 7; i++)
		{
			Q_strlcpy(d->headings[i], items[i], sizeof(d->headings[0]));
		}

		d->numheadings = i;
		MW_FreeList(items, n);
		return true;
	}

	return false;
}

static qboolean
Bot_Finish(mparse_t *p, mwidget_t *w)
{
	bot_t *d = w->data;

	if (w->w <= 0)
	{
		w->w = MD_FrameW(p->page);
	}

	if (d->numheadings * MD_FH(w) > w->minh)
	{
		w->h = w->minh = d->numheadings * MD_FH(w);
	}

	w->flags |= WF_VARIABLE;
	return true;
}

static void
Bot_Update(mpage_t *pg, mwidget_t *w)
{
	bot_t *d = w->data;

	(void)pg;

	if (d->namecvar && strcmp(d->namecvar->string, d->loaded))
	{
		Bot_Load(d);
	}
}

static void
Bot_Draw(mpage_t *pg, mwidget_t *w, int x, int y)
{
	bot_t *d = w->data;
	const char *cells[2];
	int i, L = MD_FH(w);
	mcolor_t c = (w->flags & WF_HOVER) ? w->atint : w->tint;

	if (!d->read)
	{
		return;
	}

	if (d->portrait[0])
	{
		MR_StretchPic(x + (w->w - 64) / 2, y, 64, 64, 0xFFFFFFFF, d->portrait, 0);
	}

	for (i = 0; i < d->numheadings && i < 7; i++)
	{
		cells[0] = d->headings[i];
		cells[1] = d->values[i];
		MD_Columns(pg, w, x, y + 72 + i * L, cells, 2, c);
	}
}

static const mwclass_t md_bot = {
	"bot", Bot_Option, Bot_Finish, NULL, NULL, Bot_Update, Bot_Draw, NULL, NULL, NULL
};

/* ------------------------------------------------------------------ */

qboolean
MD_DataTag(mparse_t *p, const char *tag)
{
	mwidget_t *w;
	char a[3][MENU_MAX_TOKEN];

	if (!Q_stricmp(tag, "translate"))
	{
		return Translate_Tag(p);
	}

	if (!Q_stricmp(tag, "serverbox"))
	{
		w = ML_NewWidget(p, &md_serverbox, true);
		w->data = Z_Malloc(sizeof(serverbox_t));
		((serverbox_t *)w->data)->lastcount = -1;
	}
	else if (!Q_stricmp(tag, "serverdetail") || !Q_stricmp(tag, "players"))
	{
		qboolean players = !Q_stricmp(tag, "players");
		detail_t *d;

		w = ML_NewWidget(p, players ? &md_players : &md_serverdetail, true);
		d = w->data = Z_Malloc(sizeof(detail_t));
		d->isplayers = players;
		d->lastindex = -100;
		w->w = MD_FrameW(p->page);
		w->h = w->minh = (players ? 8 : 10) * MD_FH(w);

		if (players)
		{
			w->flags |= WF_VARIABLE;
		}
	}
	else if (!Q_stricmp(tag, "chat"))
	{
		w = ML_NewWidget(p, &md_chat, true);
		w->data = Z_Malloc(sizeof(chat_t));
	}
	else if (!Q_stricmp(tag, "users") || !Q_stricmp(tag, "rooms"))
	{
		qboolean rooms = !Q_stricmp(tag, "rooms");

		w = ML_NewWidget(p, rooms ? &md_rooms : &md_users, true);
		w->data = Z_Malloc(sizeof(roster_t));
		((roster_t *)w->data)->rooms = rooms;
		((roster_t *)w->data)->lastcount = -1;
	}
	else if (!Q_stricmp(tag, "filebox") || !Q_stricmp(tag, "filereq"))
	{
		filebox_t *d;
		qboolean req = !Q_stricmp(tag, "filereq");

		MP_Args(p, a, 3);
		w = ML_NewWidget(p, req ? &md_filereq : &md_filebox, true);
		d = w->data = Z_Malloc(sizeof(filebox_t));
		d->req = req;
		Q_strlcpy(d->dir, a[0], sizeof(d->dir));
		Q_strlcpy(d->pattern, a[1], sizeof(d->pattern));
		Q_strlcpy(d->ext, a[2], sizeof(d->ext));
	}
	else if (!Q_stricmp(tag, "loadbox"))
	{
		w = ML_NewWidget(p, &md_loadbox, true);
		w->data = Z_Malloc(sizeof(loadbox_t));
	}
	else if (!Q_stricmp(tag, "listfile"))
	{
		mlist_t *d;

		MP_Args(p, a, 1);
		w = ML_NewWidget(p, &md_listfile, true);
		d = w->data = Z_Malloc(sizeof(mlist_t));

		if (!p->dry)
		{
			d->listcvar = Cvar_Get(a[0], "", 0);
		}
	}
	else if (!Q_stricmp(tag, "botlist"))
	{
		mlist_t *d;

		w = ML_NewWidget(p, &md_botlist, true);
		d = w->data = Z_Malloc(sizeof(mlist_t));
		d->bots = true;
	}
	else if (!Q_stricmp(tag, "bot"))
	{
		bot_t *d;

		MP_Args(p, a, 1);
		w = ML_NewWidget(p, &md_bot, true);
		d = w->data = Z_Malloc(sizeof(bot_t));

		if (!p->dry)
		{
			d->namecvar = Cvar_Get(a[0], "", 0);
		}
	}
	else
	{
		return false;
	}

	ML_ReadOptions(p, w);

	if (!Q_stricmp(tag, "chat") || !Q_stricmp(tag, "users") || !Q_stricmp(tag, "rooms"))
	{
		/* rest of the frame */
	}

	ML_AddWidget(p, w);
	return true;
}
