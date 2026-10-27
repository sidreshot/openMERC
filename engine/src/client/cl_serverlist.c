/*
 * Copyright (C) 2026 OpenMerc contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * OpenMerc: Internet server browser.
 *
 * Gets the server lists of every master in cvar cl_masters over TCP
 * (SPEC-204 section 2), merges them, queries each server with \status\
 * over UDP and keeps the answers for the menu (SPEC-204 section 5).
 * Console command "serverlist" runs an update and prints the result.
 *
 * Differences from the 1.07f browser, which talks to the one fixed host
 * master.gamespy.com: several masters are asked at the same time, their
 * lists are merged without duplicates before the servers are queried, and
 * the TCP steps never block the game. The per-server rules are kept: at
 * most 10 queries in flight, each from its own UDP socket, 3 s timeout,
 * no retry, servers that do not complete are dropped.
 *
 * Implements: SPEC-204
 *
 * =======================================================================
 */

#include <stdlib.h>

#include "header/client.h"
#include "header/serverlist.h"

#define SL_MAX_MASTERS 8
#define SL_SLOTS 10             /* concurrent server queries */
#define SL_QUERY_MSEC 3000      /* per-server timeout */
#define SL_MASTER_MSEC 8000     /* OpenMerc: give up on a master after this */
#define SL_LISTBUF 2047         /* list receive buffer */
#define SL_GREETING 255         /* one receive of at most 255 bytes */
#define SL_NOPING 9999

typedef enum
{
	MS_CONNECTING,
	MS_GREETING,
	MS_LIST,
	MS_DONE,
	MS_FAILED
} masterstate_t;

typedef struct
{
	char name[64];
	gsaddr_t addr;
	gssock_t sock;
	masterstate_t state;
	int start;
	byte buf[SL_LISTBUF + 1];
	int len;
	gsaddr_t *records;
	int numrecords;
	int maxrecords;
	int added;              /* records that were new to the merged list */
	const char *error;
} slmaster_t;

typedef struct
{
	gsaddr_t addr;          /* query address */
	char hostname[64];
	char mapname[64];
	char gametype[32];
	char gamever[16];
	char violence[16];
	char numplayers[8];
	char numbots[8];
	char maxplayers[8];
	char hostport[8];
} slserver_t;

typedef struct
{
	gssock_t sock;
	int server;             /* index into sl_servers, -1 = free */
	int sent;
	int ping;
} slslot_t;

static cvar_t *cl_masters;

static serverlist_state_t sl_state = SL_IDLE;
static qboolean sl_print;
static qboolean sl_transferring;

/* MENU-SCRATCH: the browser status line in the menu language (menu_omset.sp),
   the English text when the package is missing */
const char *MSP_Find(const char *ref);

const char *
SL_StatusText(const char *ref, const char *fallback)
{
	const char *s = MSP_Find(ref);

	return (s && s[0]) ? s : fallback;
}

static slmaster_t sl_masters[SL_MAX_MASTERS];
static int sl_nummasters;

static slserver_t *sl_servers;
static int sl_numservers;
static int sl_maxservers;
static int sl_next;

static slslot_t sl_slots[SL_SLOTS];

static serverlist_entry_t *sl_entries;
static int sl_numentries;
static int sl_maxentries;

/* ================================================================== */

serverlist_state_t
CL_ServerList_State(void)
{
	return sl_state;
}

int
CL_ServerList_Count(void)
{
	return sl_numentries;
}

const serverlist_entry_t *
CL_ServerList_Entry(int index)
{
	if ((index < 0) || (index >= sl_numentries))
	{
		return NULL;
	}

	return &sl_entries[index];
}

/* grows a malloc'ed array; returns false when out of memory */
static qboolean
SL_Grow(void **array, int *max, int count, size_t size)
{
	void *p;
	int newmax;

	if (count < *max)
	{
		return true;
	}

	newmax = *max ? *max * 2 : 64;
	p = realloc(*array, newmax * size);

	if (!p)
	{
		return false;
	}

	*array = p;
	*max = newmax;

	return true;
}

/* ================================================================== */
/* masters (SPEC-204 section 2)                                       */
/* ================================================================== */

static void
SL_MasterFail(slmaster_t *m, const char *error)
{
	GS_Close(m->sock);
	m->sock = GS_NOSOCK;
	m->state = MS_FAILED;
	m->error = error;
	m->numrecords = 0; /* an aborted list is not queried */
}

static void
SL_MergeMaster(slmaster_t *m)
{
	int i, j;

	for (i = 0; i < m->numrecords; i++)
	{
		for (j = 0; j < sl_numservers; j++)
		{
			if (GS_AdrEqual(&sl_servers[j].addr, &m->records[i]))
			{
				break;
			}
		}

		if (j < sl_numservers)
		{
			continue;
		}

		if (!SL_Grow((void **)&sl_servers, &sl_maxservers, sl_numservers,
				sizeof(slserver_t)))
		{
			break;
		}

		memset(&sl_servers[sl_numservers], 0, sizeof(slserver_t));
		sl_servers[sl_numservers].addr = m->records[i];
		sl_numservers++;
		m->added++;
	}
}

/* consumes the complete 6-byte records; returns true at \final\ */
static qboolean
SL_ParseList(slmaster_t *m)
{
	int p = 0;
	qboolean final = false;

	while (m->len - p >= 6)
	{
		if ((m->len - p >= 7) && !memcmp(m->buf + p, "\\final\\", 7))
		{
			final = true;
			break;
		}

		if ((m->len - p == 6) && !memcmp(m->buf + p, "\\final", 6))
		{
			break; /* the terminator may be split; wait for its last byte */
		}

		if (SL_Grow((void **)&m->records, &m->maxrecords, m->numrecords,
				sizeof(gsaddr_t)))
		{
			gsaddr_t *a = &m->records[m->numrecords++];

			memcpy(a->ip, m->buf + p, 4);
			a->port = (unsigned short)((m->buf[p + 4] << 8) | m->buf[p + 5]);
		}

		p += 6;
	}

	memmove(m->buf, m->buf + p, m->len - p);
	m->len -= p;

	return final;
}

static void
SL_SendRequest(slmaster_t *m, const byte *challenge)
{
	char validate[16];
	char msg[160];
	const char *list = "\\list\\cmp\\gamename\\" GS_GAMENAME "\\final\\";

	GS_Validate(challenge, 6, GS_SECRETKEY, validate, sizeof(validate));
	Com_sprintf(msg, sizeof(msg),
			"\\gamename\\%s\\gamever\\%s\\location\\0\\validate\\%s\\final\\\\queryid\\1.1\\",
			GS_GAMENAME, GS_SDKVER, validate);

	if ((GS_TCPSend(m->sock, msg, (int)strlen(msg)) != (int)strlen(msg)) ||
		(GS_TCPSend(m->sock, list, (int)strlen(list)) != (int)strlen(list)))
	{
		SL_MasterFail(m, "send failed");
		return;
	}

	m->state = MS_LIST;
	m->len = 0;

	if (!sl_transferring)
	{
		sl_transferring = true;
		Cvar_Set("menu_servername", SL_StatusText("MENU_OMSET_SL_TRANSFER", "Transferring Server List"));
		Com_DPrintf("GameSpy list transferring\n");
	}
}

static void
SL_MasterFrame(slmaster_t *m, int now)
{
	int n, i;

	if ((m->state == MS_DONE) || (m->state == MS_FAILED))
	{
		return;
	}

	if (now - m->start > SL_MASTER_MSEC)
	{
		SL_MasterFail(m, (m->state == MS_LIST) ? "list timed out" : "timed out");
		return;
	}

	if (m->state == MS_CONNECTING)
	{
		n = GS_TCPConnected(m->sock);

		if (n < 0)
		{
			SL_MasterFail(m, "connection failed");
			return;
		}

		if (n == 0)
		{
			return;
		}

		m->state = MS_GREETING;
	}

	if (m->state == MS_GREETING)
	{
		n = GS_TCPRecv(m->sock, m->buf, SL_GREETING);

		if (n == 0)
		{
			return;
		}

		if (n < 0)
		{
			SL_MasterFail(m, "closed before the greeting");
			return;
		}

		/* \secure\ and exactly 6 challenge characters in this one receive */
		for (i = 0; i + 8 + 6 <= n; i++)
		{
			if (!memcmp(m->buf + i, "\\secure\\", 8))
			{
				break;
			}
		}

		if (i + 8 + 6 > n)
		{
			SL_MasterFail(m, "no \\secure\\ in the greeting");
			return;
		}

		SL_SendRequest(m, m->buf + i + 8);

		if (m->state != MS_LIST)
		{
			return;
		}
	}

	/* MS_LIST */
	while ((n = GS_TCPRecv(m->sock, m->buf + m->len, SL_LISTBUF - m->len)) > 0)
	{
		m->len += n;

		if (SL_ParseList(m))
		{
			GS_Close(m->sock);
			m->sock = GS_NOSOCK;
			m->state = MS_DONE;
			SL_MergeMaster(m);
			return;
		}
	}

	if (n < 0)
	{
		/* closed before \final\: the list is aborted */
		SL_MasterFail(m, "list aborted");
	}
}

/* ================================================================== */
/* server queries                                                     */
/* ================================================================== */

/* the browser compares reply keys case-insensitively */
static void
SL_SetKey(slserver_t *s, const char *key, const char *value)
{
	if (!Q_stricmp(key, "hostname"))
	{
		Q_strlcpy(s->hostname, value, sizeof(s->hostname));
	}
	else if (!Q_stricmp(key, "mapname"))
	{
		Q_strlcpy(s->mapname, value, sizeof(s->mapname));
	}
	else if (!Q_stricmp(key, "gametype"))
	{
		Q_strlcpy(s->gametype, value, sizeof(s->gametype));
	}
	else if (!Q_stricmp(key, "gamever"))
	{
		Q_strlcpy(s->gamever, value, sizeof(s->gamever));
	}
	else if (!Q_stricmp(key, "violence"))
	{
		Q_strlcpy(s->violence, value, sizeof(s->violence));
	}
	else if (!Q_stricmp(key, "numplayers"))
	{
		Q_strlcpy(s->numplayers, value, sizeof(s->numplayers));
	}
	else if (!Q_stricmp(key, "numbots"))
	{
		Q_strlcpy(s->numbots, value, sizeof(s->numbots));
	}
	else if (!Q_stricmp(key, "maxplayers"))
	{
		Q_strlcpy(s->maxplayers, value, sizeof(s->maxplayers));
	}
	else if (!Q_stricmp(key, "hostport"))
	{
		Q_strlcpy(s->hostport, value, sizeof(s->hostport));
	}
}

static const char *
SL_Or(const char *value, const char *def)
{
	return value[0] ? value : def;
}

/* hand-off of a completed server (SPEC-204 section 5, step 5) */
static void
SL_AddEntry(const slserver_t *s, int ping)
{
	serverlist_entry_t *e;
	gsaddr_t game;
	int i;

	game = s->addr;
	game.port = s->hostport[0] ? (unsigned short)atoi(s->hostport) : GS_PORT_GAME;

	/* the menu keeps one record per address */
	for (i = 0; i < sl_numentries; i++)
	{
		if (GS_AdrEqual(&sl_entries[i].game, &game))
		{
			return;
		}
	}

	if (!SL_Grow((void **)&sl_entries, &sl_maxentries, sl_numentries,
			sizeof(serverlist_entry_t)))
	{
		return;
	}

	e = &sl_entries[sl_numentries++];
	memset(e, 0, sizeof(*e));
	e->query = s->addr;
	e->game = game;
	Q_strlcpy(e->hostname, SL_Or(s->hostname, "localhost"), sizeof(e->hostname));
	Q_strlcpy(e->mapname, s->mapname, sizeof(e->mapname));
	Q_strlcpy(e->gametype, SL_Or(s->gametype, "DM"), sizeof(e->gametype));
	Q_strlcpy(e->gamever, s->gamever, sizeof(e->gamever));
	Q_strlcpy(e->violence, SL_Or(s->violence, "0"), sizeof(e->violence));
	e->numplayers = atoi(s->numplayers);
	e->numbots = atoi(s->numbots);
	e->maxplayers = atoi(s->maxplayers);
	e->ping = ping;

	Com_sprintf(e->text, sizeof(e->text), "%s+%s+%s/%s/%s++%s+%s+%i+",
			e->hostname, e->mapname, SL_Or(s->numplayers, "0"), SL_Or(s->numbots, "0"),
			SL_Or(s->maxplayers, "0"), e->gametype, e->violence, ping);

	Com_DPrintf("... add %s\n", e->text);
}

static void
SL_FreeSlot(slslot_t *slot)
{
	slot->server = -1;
}

static void
SL_SlotFrame(slslot_t *slot, int now)
{
	char buf[1500];
	char key[64], value[256];
	const char *p;
	slserver_t *s;
	gsaddr_t from;
	qboolean final;
	int len;

	if (slot->server < 0)
	{
		/* a free slot takes the next server of the list */
		if (sl_next >= sl_numservers)
		{
			return;
		}

		if (slot->sock == GS_NOSOCK)
		{
			slot->sock = GS_UDPOpen(0, NULL);

			if (slot->sock == GS_NOSOCK)
			{
				return;
			}
		}

		slot->server = sl_next++;
		slot->sent = now;
		slot->ping = SL_NOPING;
		GS_UDPSend(slot->sock, &sl_servers[slot->server].addr, "\\status\\", 8);
		return;
	}

	s = &sl_servers[slot->server];

	/* at most one datagram (<= 1499 bytes) per socket and frame */
	len = GS_UDPRecv(slot->sock, &from, buf, sizeof(buf) - 1);

	if (len == -2)
	{
		SL_FreeSlot(slot); /* a receive error drops the server */
		return;
	}

	/* only datagrams from exactly that address and port */
	if ((len >= 0) && GS_AdrEqual(&from, &s->addr))
	{
		buf[len] = 0;

		if (slot->ping == SL_NOPING)
		{
			slot->ping = Sys_Milliseconds() - slot->sent;
		}

		final = false;

		for (p = buf; (p = GS_NextPair(p, key, sizeof(key), value, sizeof(value))) != NULL;)
		{
			if (!strcmp(key, "final"))
			{
				final = true;
			}
			else if (strcmp(key, "queryid"))
			{
				SL_SetKey(s, key, value);
			}
		}

		if (final || strstr(buf, "\\final\\"))
		{
			SL_AddEntry(s, slot->ping);
			SL_FreeSlot(slot);
			return;
		}
	}

	if (now - slot->sent >= SL_QUERY_MSEC)
	{
		SL_FreeSlot(slot); /* dropped silently, no retry */
	}
}

/* ================================================================== */
/* update                                                             */
/* ================================================================== */

static void
SL_Reset(void)
{
	int i;

	for (i = 0; i < sl_nummasters; i++)
	{
		GS_Close(sl_masters[i].sock);
		free(sl_masters[i].records);
	}

	memset(sl_masters, 0, sizeof(sl_masters));
	sl_nummasters = 0;

	for (i = 0; i < SL_SLOTS; i++)
	{
		if (sl_slots[i].sock != GS_NOSOCK)
		{
			GS_Close(sl_slots[i].sock);
		}

		sl_slots[i].sock = GS_NOSOCK;
		sl_slots[i].server = -1;
	}

	sl_numservers = 0;
	sl_next = 0;
	sl_transferring = false;
}

static int
SL_EntryCompare(const void *a, const void *b)
{
	const serverlist_entry_t *x = a, *y = b;

	return x->ping - y->ping;
}

static void
SL_PrintableName(char *out, int size, const char *in)
{
	int n = 0;

	/* SoF colour codes are bytes below 32 */
	for (; *in && n < size - 1; in++)
	{
		if ((unsigned char)*in >= 32)
		{
			out[n++] = *in;
		}
	}

	out[n] = 0;
}

static void
SL_PrintList(void)
{
	char name[64];
	char players[16];
	int i;

	Com_Printf("\nGameSpy masters:\n");

	for (i = 0; i < sl_nummasters; i++)
	{
		slmaster_t *m = &sl_masters[i];

		if (m->state == MS_DONE)
		{
			Com_Printf("  %-26s %3i servers (%i new)\n", m->name, m->numrecords, m->added);
		}
		else
		{
			Com_Printf("  %-26s %s\n", m->name, m->error ? m->error : "failed");
		}
	}

	qsort(sl_entries, sl_numentries, sizeof(serverlist_entry_t), SL_EntryCompare);

	Com_Printf("%i servers listed, %i answered\n", sl_numservers, sl_numentries);

	if (!sl_numentries)
	{
		return;
	}

	Com_Printf("ping players map              name                       address\n");

	for (i = 0; i < sl_numentries; i++)
	{
		const serverlist_entry_t *e = &sl_entries[i];

		SL_PrintableName(name, 27, e->hostname);
		Com_sprintf(players, sizeof(players), "%i/%i", e->numplayers, e->maxplayers);
		Com_Printf("%4i %-7s %-16.16s %-26s %s\n", e->ping, players, e->mapname,
				name, GS_AdrToString(&e->game));
	}
}

static void
SL_Finish(qboolean reached)
{
	sl_state = SL_IDLE;

	if (reached)
	{
		Cvar_Set("menu_servername", SL_StatusText("MENU_OMSET_SL_DONE", "Query Complete"));
		Com_DPrintf("GameSpy query complete\n");
	}

	if (sl_print)
	{
		SL_PrintList(); /* before the reset: it reports the masters */
	}

	SL_Reset();
}

void
CL_ServerList_Refresh(qboolean print)
{
	char name[64];
	const char *s;
	slmaster_t *m;
	int n, now;

	if (sl_state != SL_IDLE)
	{
		Com_Printf("GameSpy is busy .....\n");
		return;
	}

	SL_Reset();
	sl_numentries = 0;
	sl_print = print;
	now = Sys_Milliseconds();

	Com_Printf(".... finding GameSpy servers ....\n");

	for (s = cl_masters->string; *s;)
	{
		while (*s == ' ' || *s == '\t' || *s == ',' || *s == ';')
		{
			s++;
		}

		for (n = 0; *s && *s != ' ' && *s != '\t' && *s != ',' && *s != ';'; s++)
		{
			if (n < (int)sizeof(name) - 1)
			{
				name[n++] = *s;
			}
		}

		name[n] = 0;

		if (!n || (sl_nummasters == SL_MAX_MASTERS))
		{
			continue;
		}

		m = &sl_masters[sl_nummasters++];
		Q_strlcpy(m->name, name, sizeof(m->name));
		m->sock = GS_NOSOCK;
		m->start = now;

		if (!GS_Resolve(name, GS_PORT_LIST, &m->addr))
		{
			m->state = MS_FAILED;
			m->error = "cannot resolve";
			continue;
		}

		m->sock = GS_TCPConnect(&m->addr);

		if (m->sock == GS_NOSOCK)
		{
			m->state = MS_FAILED;
			m->error = "connection failed";
			continue;
		}

		m->state = MS_CONNECTING;
	}

	sl_state = SL_TRANSFERRING;
}

void
CL_ServerList_Frame(void)
{
	qboolean busy = false;
	int i, now;

	if (sl_state == SL_IDLE)
	{
		return;
	}

	now = Sys_Milliseconds();

	for (i = 0; i < sl_nummasters; i++)
	{
		SL_MasterFrame(&sl_masters[i], now);

		if ((sl_masters[i].state != MS_DONE) && (sl_masters[i].state != MS_FAILED))
		{
			busy = true;
		}
	}

	if (!busy && (sl_state == SL_TRANSFERRING))
	{
		if (!sl_transferring)
		{
			/* no master got past the request: the list stays idle */
			SL_Finish(false);
			return;
		}

		sl_state = SL_QUERYING;
		Cvar_Set("menu_servername", SL_StatusText("MENU_OMSET_SL_QUERY", "Querying master servers"));
		Com_DPrintf("GameSpy master query\n");
	}

	/* servers of completed lists are queried while other masters still talk */
	for (i = 0; i < SL_SLOTS; i++)
	{
		SL_SlotFrame(&sl_slots[i], now);

		if (sl_slots[i].server >= 0)
		{
			busy = true;
		}
	}

	if (!busy && (sl_next >= sl_numservers))
	{
		SL_Finish(true);
	}
}

static void
CL_ServerList_f(void)
{
	CL_ServerList_Refresh(true);
}

void
CL_ServerList_Init(void)
{
	int i;

	for (i = 0; i < SL_SLOTS; i++)
	{
		sl_slots[i].sock = GS_NOSOCK;
		sl_slots[i].server = -1;
	}

	cl_masters = Cvar_Get("cl_masters", GS_DEFAULT_MASTERS, CVAR_ARCHIVE);
	Cmd_AddCommand("serverlist", CL_ServerList_f);
}
