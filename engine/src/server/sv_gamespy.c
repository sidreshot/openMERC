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
 * OpenMerc: GameSpy query responder and heartbeats.
 *
 * Every process opens the query socket at start-up and answers GameSpy
 * queries on it (SPEC-204 section 4). A public multiplayer server sends
 * heartbeats from that socket to every master in cvar sv_masters
 * (SPEC-204 section 3). The original sends them to the fixed host
 * master.gamespy.com, which no longer exists; sv_masters is an OpenMerc
 * extension that defaults to today's community masters.
 *
 * Implements: SPEC-204, SPEC-200, SPEC-AMEND-005 (GS.13, GS.14)
 *
 * =======================================================================
 */

#include <stdarg.h>

#include "header/server.h"
#include "../common/header/gamespy.h"

#define GS_MAX_MASTERS 8
#define GS_QUERY_READ 255       /* query read buffer; longer datagrams are ignored */
#define GS_DATA_LIMIT 1349      /* data bytes per reply datagram */
#define GS_SPLIT_LIMIT 1350     /* longest fragment that is not split */
#define GS_FRAGMENT_SIZE 256    /* every fragment is formatted into this */
#define GS_ECHO_LIMIT 1350
#define GS_SECURE_LIMIT 128
#define GS_PORT_SEARCHES 5      /* searches from gamespyport + 0 ... + 4 */
#define GS_PORT_SEARCH 100      /* ports per search */
#define GS_RESOLVE_MSEC (30 * 60 * 1000)

typedef struct
{
	gsaddr_t to;
	char data[GS_SPLIT_LIMIT + 64];
	int len;
	int packet;
} gsreply_t;

static cvar_t *gamespyport;
static cvar_t *sv_masters;
static cvar_t *sv_violence;
static cvar_t *sv_public;
static cvar_t *sv_hostname;

static gssock_t gs_socket = GS_NOSOCK;
static int gs_port;
static unsigned int gs_queryid = 1;
static qboolean gs_exiting;

static gsaddr_t gs_masters[GS_MAX_MASTERS];
static int gs_nummasters;
static qboolean gs_resolved;
static int gs_resolvetime;

static int gs_lastcount = -1;  /* spawned clients at the last heartbeat; -1 = unknown */
static int gs_last_heartbeat = -99999; /* svs.realtime of the last heartbeat; < 0 = long ago */
static qboolean gs_running;

static const char *(*gs_gametype)(void);

/* ================================================================== */

void
SV_GameSpy_SetGameTypeFunc(const char *(*func)(void))
{
	gs_gametype = func;
}

gssock_t
SV_GameSpy_Socket(void)
{
	return gs_socket;
}

static void
SV_GameSpy_OpenSocket(void)
{
	int base = (int)gamespyport->value;
	int k, i;

	if (base < 0)
	{
		base = GS_PORT_QUERY; /* OpenMerc: not specified for the original */
	}

	/* always bound to all interfaces, also when cvar ip names one address
	   (SPEC-AMEND-005 GS.13) */
	if (base == 0)
	{
		/* the operating system chooses the port (SPEC-AMEND-005 GS.14) */
		gs_socket = GS_UDPOpen(0, &gs_port);

		if (gs_socket != GS_NOSOCK)
		{
			Com_DPrintf("GameSpy query port %i\n", gs_port);
			return;
		}
	}

	/* up to five searches: search k tries gamespyport + k ... + k + 99 and
	   stops at the first port that binds, so the ports reached are
	   gamespyport + 0 ... + 103 (SPEC-204 section 3, SPEC-AMEND-005 GS.14).
	   Repeating a port that failed before is harmless: it fails again. */
	for (k = 0; (base > 0) && (k < GS_PORT_SEARCHES); k++)
	{
		for (i = 0; i < GS_PORT_SEARCH; i++)
		{
			int port = base + k + i;

			if (port > 65535)
			{
				break;
			}

			gs_socket = GS_UDPOpen(port, &gs_port);

			if (gs_socket != GS_NOSOCK)
			{
				Com_DPrintf("GameSpy query port %i\n", gs_port);
				return;
			}
		}
	}

	Com_Printf("GameSpy: no free query port from %i\n", base);
}

void
SV_GameSpy_Init(void)
{
	gamespyport = Cvar_Get("gamespyport", va("%i", GS_PORT_QUERY), CVAR_NOSET);
	sv_masters = Cvar_Get("sv_masters", GS_DEFAULT_MASTERS, CVAR_ARCHIVE);
	sv_violence = Cvar_Get("sv_violence", "0", CVAR_SERVERINFO | CVAR_LATCH);
	sv_public = Cvar_Get("public", "0", 0);
	sv_hostname = Cvar_Get("hostname", "noname", CVAR_SERVERINFO | CVAR_ARCHIVE);

	if (gs_socket == GS_NOSOCK)
	{
		SV_GameSpy_OpenSocket();
	}
}

/* ================================================================== */
/* reply assembly (SPEC-204 section 4)                                */
/* ================================================================== */

static void
GS_Flush(gsreply_t *r)
{
	if (!r->len)
	{
		return;
	}

	r->packet++;
	Com_sprintf(r->data + r->len, (int)sizeof(r->data) - r->len,
			"\\queryid\\%u.%i", gs_queryid, r->packet);
	r->len += (int)strlen(r->data + r->len);
	GS_UDPSend(gs_socket, &r->to, r->data, r->len);
	r->len = 0;
}

static void
GS_Append(gsreply_t *r, const char *frag, int len)
{
	int i, n, cut;

	while (len > 0)
	{
		if (r->len + len <= GS_DATA_LIMIT)
		{
			memcpy(r->data + r->len, frag, len);
			r->len += len;
			return;
		}

		if (len <= GS_SPLIT_LIMIT)
		{
			GS_Flush(r);
			memcpy(r->data, frag, len);
			r->len = len;
			return;
		}

		/* cut at the last backslash that begins a key (backslashes 0, 2,
		   4, ... of the fragment) within the first GS_SPLIT_LIMIT bytes */
		cut = 0;

		for (i = 0, n = 0; i < GS_SPLIT_LIMIT; i++)
		{
			if (frag[i] == '\\')
			{
				if (!(n & 1))
				{
					cut = i;
				}

				n++;
			}
		}

		if (cut == 0)
		{
			return; /* dropped */
		}

		GS_Append(r, frag, cut);
		frag += cut;
		len -= cut;
	}
}

static void
GS_Fragment(gsreply_t *r, const char *fmt, ...)
{
	char frag[GS_FRAGMENT_SIZE];
	va_list argptr;
	int len;

	va_start(argptr, fmt);
	len = vsnprintf(frag, sizeof(frag), fmt, argptr);
	va_end(argptr);

	if (len < 0)
	{
		return;
	}

	if (len >= (int)sizeof(frag))
	{
		len = (int)sizeof(frag) - 1; /* truncated like the original */
	}

	GS_Append(r, frag, len);
}

static int
GS_SpawnedClients(void)
{
	int i, count = 0;

	if (!svs.initialized || !svs.clients)
	{
		return 0;
	}

	for (i = 0; i < svs.numclients; i++)
	{
		if (svs.clients[i].state == cs_spawned)
		{
			count++;
		}
	}

	return count;
}

/* the port of the game socket, chosen as in backends/<os>/network.c */
static int
GS_GamePort(void)
{
	int port = (int)Cvar_VariableValue("ip_hostport");

	if (!port)
	{
		port = (int)Cvar_VariableValue("hostport");
	}

	if (!port)
	{
		port = (int)Cvar_VariableValue("port");
	}

	return port ? port : PORT_SERVER;
}

static qboolean
GS_ServerRunning(void)
{
	return svs.initialized && (sv.state != ss_dead);
}

static void
GS_Basic(gsreply_t *r)
{
	if (GS_ServerRunning())
	{
		GS_Fragment(r, "\\gamename\\%s\\gamever\\%s\\location\\1", GS_GAMENAME,
				GS_GAMEVER);
	}
}

static void
GS_Info(gsreply_t *r)
{
	if (!GS_ServerRunning())
	{
		return;
	}

	GS_Fragment(r, "\\hostname\\%s", sv_hostname->string);
	GS_Fragment(r, "\\hostport\\%u", (unsigned)GS_GamePort());
	GS_Fragment(r, "\\mapname\\%s", Cvar_VariableString("mapname"));

	if (ge && gs_gametype)
	{
		GS_Fragment(r, "\\gametype\\%s", gs_gametype());
	}
	else
	{
		/* no game-type name from the game module: deathmatch as an integer */
		GS_Fragment(r, "\\gametype\\%i", (int)Cvar_VariableValue("deathmatch"));
	}

	GS_Fragment(r, "\\numplayers\\%i", GS_SpawnedClients());
	GS_Fragment(r, "\\maxplayers\\%i", (int)maxclients->value);
	GS_Fragment(r, "\\gamemode\\%s", gs_exiting ? "exiting" : "openplaying");
	GS_Fragment(r, "\\violence\\%s", sv_violence->string);
}

static void
GS_Rules(gsreply_t *r)
{
	if (!GS_ServerRunning())
	{
		return;
	}

	/* integer values are truncated; movescale is truncated before the
	   multiplication. Cvars the game module did not register read as 0. */
	GS_Fragment(r, "\\timelimit\\%i", (int)Cvar_VariableValue("timelimit"));
	GS_Fragment(r, "\\fraglimit\\%i", (int)Cvar_VariableValue("fraglimit"));
	GS_Fragment(r, "\\dmflags\\%i", (int)Cvar_VariableValue("dmflags"));
	GS_Fragment(r, "\\movescale\\%i%%", (int)Cvar_VariableValue("g_movescale") * 100);
	GS_Fragment(r, "\\cheats\\%s", Cvar_VariableValue("cheats") ? "enabled" : "disabled");
	GS_Fragment(r, "\\ctf_loops\\%i", (int)Cvar_VariableValue("ctf_loops"));
	GS_Fragment(r, "\\suicide_penalty\\%i", (int)Cvar_VariableValue("sv_suicidepenalty"));
}

static void
GS_Players(gsreply_t *r)
{
	char name[MAX_INFO_VALUE], skin[MAX_INFO_VALUE], team[MAX_INFO_VALUE];
	client_t *cl;
	edict_t *ent;
	int i, frags;

	if (!GS_ServerRunning())
	{
		return;
	}

	for (i = 0; i < svs.numclients; i++)
	{
		cl = &svs.clients[i];

		if (cl->state != cs_spawned)
		{
			continue;
		}

		/* the raw userinfo name: cl->name has its high bits masked off */
		Q_strlcpy(name, Info_ValueForKey(cl->userinfo, "name"), sizeof(name));
		Q_strlcpy(skin, Info_ValueForKey(cl->userinfo, "skin"), sizeof(skin));
		Q_strlcpy(team, Info_ValueForKey(cl->userinfo, "teamname"), sizeof(team));

		/* the score as a signed 16-bit value, from the same field the
		   console status uses */
		ent = CL_EDICT(cl);
		frags = (ent && ent->client) ? (short)SV_ClientScore(cl) : 0;

		GS_Fragment(r, "\\player_%i\\%s\\frags_%i\\%i\\ping_%i\\%i\\skin_%i\\%s\\team_%i\\%s",
				i, name, i, frags, i, cl->ping, i, skin, i, team);
	}
}

/* The value of \word\ in a query: the text after the closing backslash up to
   the next backslash or the end. Returns false if the word is absent. */
static qboolean
GS_QueryWord(const char *query, const char *word, char *value, int valuesize)
{
	char pattern[16];
	const char *p;
	int n = 0;

	Com_sprintf(pattern, sizeof(pattern), "\\%s\\", word);
	p = strstr(query, pattern);

	if (!p)
	{
		return false;
	}

	if (value)
	{
		for (p += strlen(pattern); *p && (*p != '\\'); p++)
		{
			if (n < valuesize - 1)
			{
				value[n++] = *p;
			}
		}

		value[n] = 0;
	}

	return true;
}

static void
GS_HandleQuery(const gsaddr_t *from, const char *query)
{
	char echo[GS_QUERY_READ + 1];
	char secure[GS_QUERY_READ + 1];
	char validate[GS_FRAGMENT_SIZE];
	gsreply_t r;
	qboolean hasecho, hassecure;

	memset(&r, 0, sizeof(r));
	r.to = *from;

	/* fixed order, each word at most once, wherever it is in the query */
	if (GS_QueryWord(query, "basic", NULL, 0))
	{
		GS_Basic(&r);
	}

	if (GS_QueryWord(query, "info", NULL, 0))
	{
		GS_Info(&r);
	}

	if (GS_QueryWord(query, "rules", NULL, 0))
	{
		GS_Rules(&r);
	}

	if (GS_QueryWord(query, "players", NULL, 0))
	{
		GS_Players(&r);
	}

	if (GS_QueryWord(query, "status", NULL, 0))
	{
		GS_Basic(&r);
		GS_Info(&r);
		GS_Rules(&r);
		GS_Players(&r);
	}

	if (GS_QueryWord(query, "packets", NULL, 0))
	{
		GS_Basic(&r);
		GS_Flush(&r);
		GS_Info(&r);
		GS_Flush(&r);
		GS_Rules(&r);
		GS_Flush(&r);
		GS_Players(&r);
	}

	hasecho = GS_QueryWord(query, "echo", echo, sizeof(echo));

	if (hasecho && (strlen(echo) <= GS_ECHO_LIMIT))
	{
		GS_Fragment(&r, "\\echo\\%s", echo);
	}

	hassecure = GS_QueryWord(query, "secure", secure, sizeof(secure));

	if (hassecure && secure[0])
	{
		if (strlen(secure) > GS_SECURE_LIMIT)
		{
			return; /* the original stops here and sends nothing more */
		}

		GS_Validate((const unsigned char *)secure, (int)strlen(secure),
				GS_SECRETKEY, validate, sizeof(validate));
		GS_Fragment(&r, "\\validate\\%s", validate);
	}

	GS_Fragment(&r, "\\final\\");
	GS_Flush(&r);
}

/* ================================================================== */
/* heartbeats (SPEC-204 section 3)                                    */
/* ================================================================== */

static void
GS_ResolveMasters(void)
{
	char name[256];
	const char *s;
	int n;

	gs_nummasters = 0;
	s = sv_masters->string;

	while (*s)
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

		if (!n)
		{
			continue;
		}

		if (gs_nummasters == GS_MAX_MASTERS)
		{
			Com_Printf("GameSpy: more than %i masters in sv_masters, ignoring %s\n",
					GS_MAX_MASTERS, name);
			continue;
		}

		if (!GS_Resolve(name, GS_PORT_HEARTBEAT, &gs_masters[gs_nummasters]))
		{
			Com_Printf("GameSpy: cannot resolve master %s\n", name);
			continue;
		}

		Com_DPrintf("GameSpy master %s = %s\n", name,
				GS_AdrToString(&gs_masters[gs_nummasters]));
		gs_nummasters++;
	}

	sv_masters->modified = false;
	gs_resolved = true;
	gs_resolvetime = Sys_Milliseconds();
}

static void
GS_SendHeartbeats(void)
{
	char msg[128];
	int i, len;

	if (gs_socket == GS_NOSOCK)
	{
		return;
	}

	if (!gs_resolved || sv_masters->modified ||
		(Sys_Milliseconds() - gs_resolvetime > GS_RESOLVE_MSEC))
	{
		GS_ResolveMasters();
	}

	Com_sprintf(msg, sizeof(msg), "\\heartbeat\\%i\\gamename\\%s\\statechanged\\",
			gs_port, GS_GAMENAME);
	len = (int)strlen(msg);

	Com_DPrintf("Sending heartbeat to GameSpy...\n");

	for (i = 0; i < gs_nummasters; i++)
	{
		GS_UDPSend(gs_socket, &gs_masters[i], msg, len);
	}
}

/* Called once per server frame (Master_Heartbeat). */
void
SV_GameSpy_CheckHeartbeat(void)
{
	int count;

	if (!sv_public || !sv_public->value || !maxclients || (maxclients->value <= 1))
	{
		return;
	}

	count = GS_SpawnedClients();

	/* check for time wraparound */
	if (gs_last_heartbeat > svs.realtime)
	{
		gs_last_heartbeat = svs.realtime;
	}

	/* "heartbeat" (-9999999) and a new game (SV_InitGame: -99999) set
	   last_heartbeat below zero, which means "long ago": send now */
	if ((gs_last_heartbeat >= 0) && (count == gs_lastcount) &&
		(svs.realtime - gs_last_heartbeat < GS_HEARTBEAT_MSEC))
	{
		return;
	}

	gs_last_heartbeat = svs.realtime;
	gs_lastcount = count;
	GS_SendHeartbeats();
}

/* "heartbeat" and a new game: the next check sends one */
void
SV_GameSpy_ForceHeartbeat(void)
{
	gs_last_heartbeat = -9999999;
}

/* Called every frame of every process, server running or not. */
void
SV_GameSpy_Frame(void)
{
	char buf[GS_QUERY_READ + 1];
	gsaddr_t from;
	int len;

	if (svs.initialized != gs_running)
	{
		gs_running = svs.initialized;
		gs_lastcount = -1; /* unknown again: the first check sends one */
	}

	if (gs_socket == GS_NOSOCK)
	{
		return;
	}

	/* all pending datagrams */
	while ((len = GS_UDPRecv(gs_socket, &from, buf, GS_QUERY_READ)) != -1)
	{
		if (len < 0)
		{
			continue; /* too long or a stale ICMP report: ignored */
		}

		buf[len] = 0;
		gs_queryid++;
		GS_HandleQuery(&from, buf);
	}
}

/* Engine shutdown: one more heartbeat, then the socket is closed. */
void
SV_GameSpy_Shutdown(void)
{
	if (gs_socket == GS_NOSOCK)
	{
		return;
	}

	gs_exiting = true;

	/* Deliberate difference: the original sends this heartbeat from every
	   instance, even a client or a private server (SPEC-204 section 2, test
	   vector 8); it went to master.gamespy.com, which no longer resolves.
	   With working community masters configured that would make every
	   OpenMerc process report itself to third-party hosts when it quits,
	   so only a public multiplayer server sends it. */
	if (sv_public && sv_public->value && maxclients && (maxclients->value > 1))
	{
		GS_SendHeartbeats();
	}

	GS_Close(gs_socket);
	gs_socket = GS_NOSOCK;
}
