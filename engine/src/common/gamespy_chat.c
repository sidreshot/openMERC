/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc: GameSpy lobby chat - the IRC client behind the gsc_* commands
 * of Soldier of Fortune 1.07f (SPEC-204 section 5). It keeps the room list,
 * the user list of the current channel and the chat lines for the menus'
 * rooms, users and chat widgets (SPEC-803 section 3, client/menu/m_data.c).
 *
 * Plain IRC (RFC 1459 lines, no CRYPT handshake) over TCP to
 * gsc_server:gsc_port. Messages, colours, quirks and the 30 s request limit
 * follow SPEC-204 section 5 as amended by SPEC-AMEND-006 (items I2d.1-I2d.8)
 * and black-box sessions of the original client (docs/impl/I2d.md):
 * - requests (LIST, JOIN, PART, NAMES, TOPIC, WHOIS, KICK, MODE, messages)
 *   are sent only while the session is registered (after 001);
 * - every request waits on its own; replies go to the oldest matching one,
 *   and each reply restarts its 30 s limit;
 * - results and events that name a channel are delivered only while the
 *   client is in that channel (held back while its join completes).
 *
 * Differences from 1.07f (docs/DEVIATIONS.md):
 * - gsc_server defaults to GSC_DEFAULT_SERVER (GameSpy's peerchat is gone);
 *   the stored old default is replaced at start;
 * - the connect does not block the frame (30 s limit);
 * - gsc_nick follows only the own nick changes, not every user's.
 *
 * Implements: SPEC-204 (section 5), SPEC-302 (gsc_* cvars and commands),
 *             SPEC-803 (section 3, data of the lobby widgets),
 *             SPEC-AMEND-006 (I2d.1-I2d.7)
 */

#ifdef _WIN32
 #include <winsock2.h>
#else
 #include <sys/types.h>
 #include <sys/socket.h>
#endif

#include "header/common.h"
#include "header/gamespy.h"
#include "header/gamespy_chat.h"

#define GSC_REALNAME "SoldierofFortune"
#define GSC_REQUEST_MSEC 30000  /* channel list, join, names */
#define GSC_CONNECT_MSEC 30000  /* OpenMerc: the TCP connect does not block */
#define GSC_WRITE_CHUNK 1024
#define GSC_OUTBUF 32768
#define GSC_INBUF 8192
#define GSC_NAMELEN 64
#define GSC_TEXTLEN 1024
#define GSC_MAXNAMES 8          /* NAMES requests waiting at the same time */
#define GSC_TOPIC_MAX 63        /* menu_topic */
#define GSC_MAXHELD 64          /* channel events held back during a join */

/* colour codes of the chat lines (SPEC-204 section 5, SPEC-002) */
#define C_OP "\x01"
#define C_ERROR "\x02"
#define C_BRACKET "\x03"
#define C_STATUS "\x04"
#define C_ACTION "\x06"
#define C_HILITE "\x07"
#define C_NORMAL "\x1c"

typedef enum
{
	GC_NONE,        /* no session */
	GC_CONNECTING,  /* OpenMerc: TCP connect in progress */
	GC_REGISTERING, /* NICK and USER sent, no welcome yet */
	GC_ONLINE,      /* welcome received: registered */
	GC_REFUSED,     /* ERROR before the welcome: never registered, the socket
	                   stays open, only PING is still answered (I2d.1) */
	GC_CLOSED       /* ended by the server; the session lasts until the next
	                   gsc_connect / gsc_disconnect, which report it again */
} gcstate_t;

typedef struct
{
	char nick[GSC_NAMELEN];
	qboolean op;
	qboolean voice;
} gcmember_t;

typedef struct
{
	gcmember_t *m;
	int num;
	int max;
} gclist_t;

typedef struct
{
	qboolean active;
	char channel[GSC_NAMELEN];  /* without '#', as given */
	int start;                  /* sending or last matched reply */
	unsigned seq;               /* order of sending: the oldest match first */
	gclist_t names;
} gcnames_t;

static cvar_t *gsc_server, *gsc_port, *gsc_nick, *gsc_user;

static gcstate_t gc_state;
static gssock_t gc_sock = GS_NOSOCK;
static int gc_connectstart;
static char gc_server[256];      /* of the session, for the messages */
static int gc_port;
static char gc_me[GSC_NAMELEN];  /* own nick in this session */
static char gc_out[GSC_OUTBUF];
static int gc_outlen;
static char gc_in[GSC_INBUF];
static int gc_inlen;
static qboolean gc_shutdown;

/* rooms (SPEC-204 5.2, SPEC-AMEND-006 I2d.4). Every LIST is a request of its
   own, answered in order: its own count of 322 replies and its own 30 s limit,
   restarted by every reply it gets; each room goes to the menus' room list at
   once (two requests at once list every room twice and print two counts) */
#define GSC_MAXLISTS 8
static int gc_liststart[GSC_MAXLISTS];  /* oldest first */
static int gc_listcount[GSC_MAXLISTS];
static int gc_numlists;

/* the current channel: kept by gsc_part, a kick and a disconnect */
static char gc_channel[GSC_NAMELEN];
static qboolean gc_inchannel;    /* joined, user list known */
static gclist_t gc_members;
static char gc_topic[GSC_TEXTLEN];

/* a join in progress (SPEC-204 5.3) */
static qboolean gc_joining;
static qboolean gc_joinecho;
static char gc_joinchan[GSC_NAMELEN];
static int gc_joinstart;
static gclist_t gc_joinnames;
static char gc_jointopic[GSC_TEXTLEN];

/* events naming the channel being joined, held until the join completes
   (SPEC-AMEND-006 I2d.6) */
static char *gc_held[GSC_MAXHELD];
static int gc_numheld;

static gcnames_t gc_names[GSC_MAXNAMES];
static unsigned gc_namesseq;

/* ================================================================== */
/* helpers                                                            */
/* ================================================================== */

static void
GC_ListClear(gclist_t *l)
{
	free(l->m);
	l->m = NULL;
	l->num = l->max = 0;
}

static int
GC_ListFind(const gclist_t *l, const char *nick)
{
	int i;

	for (i = 0; i < l->num; i++)
	{
		if (!Q_stricmp(l->m[i].nick, nick))
		{
			return i;
		}
	}

	return -1;
}

static gcmember_t *
GC_ListAdd(gclist_t *l, const char *nick)
{
	int i = GC_ListFind(l, nick);

	if (i >= 0)
	{
		return &l->m[i];
	}

	if (l->num == l->max)
	{
		int newmax = l->max ? l->max * 2 : 32;
		gcmember_t *p = realloc(l->m, newmax * sizeof(gcmember_t));

		if (!p)
		{
			return NULL;
		}

		l->m = p;
		l->max = newmax;
	}

	memset(&l->m[l->num], 0, sizeof(gcmember_t));
	Q_strlcpy(l->m[l->num].nick, nick, GSC_NAMELEN);

	return &l->m[l->num++];
}

static void
GC_ListRemove(gclist_t *l, int i)
{
	if ((i < 0) || (i >= l->num))
	{
		return;
	}

	memmove(&l->m[i], &l->m[i + 1], (l->num - i - 1) * sizeof(gcmember_t));
	l->num--;
}

static void
GC_ListCopy(gclist_t *to, const gclist_t *from)
{
	int i;

	GC_ListClear(to);

	for (i = 0; i < from->num; i++)
	{
		gcmember_t *m = GC_ListAdd(to, from->m[i].nick);

		if (m)
		{
			*m = from->m[i];
		}
	}
}

/* message text for display: control characters removed (SPEC-204 5.5) */
static void
GC_Clean(char *out, int size, const char *in)
{
	int n = 0;

	for (; *in && (n < size - 1); in++)
	{
		if ((unsigned char)*in >= 32)
		{
			out[n++] = *in;
		}
	}

	out[n] = 0;
}

static qboolean
GC_IsChannel(const char *s)
{
	return (s[0] == '#') || (s[0] == '&');
}

/* "#name" of the server equals the channel given without '#' */
static qboolean
GC_SameChannel(const char *server, const char *given)
{
	if (!server[0] || !given[0])
	{
		return false;
	}

	return !Q_stricmp(server + 1, given);
}

/* ---- menu side (not in a dedicated server) ---- */

static void
GC_Public(const char *text)
{
#ifndef DEDICATED_ONLY
	if (!gc_shutdown)
	{
		MC_PublicLine(text);
	}
#endif
}

static void
GC_Private(const char *text)
{
#ifndef DEDICATED_ONLY
	if (!gc_shutdown)
	{
		MC_PrivateLine(text);
	}
#endif
}

static const char *
GC_Selected(void)
{
#ifndef DEDICATED_ONLY
	if (!gc_shutdown)
	{
		return MC_SelectedUser();
	}
#endif

	return "";
}

/* the room and user lists live in the menus (SPEC-AMEND-006 I2d.7): while no
   menu is open these operations are ignored there */
static void
GC_ClearRooms(void)
{
#ifndef DEDICATED_ONLY
	if (!gc_shutdown)
	{
		MC_ClearRooms();
	}
#endif
}

static void
GC_AddRoom(const char *room)
{
#ifndef DEDICATED_ONLY
	if (!gc_shutdown)
	{
		MC_AddRoom(room);
	}
#else
	(void)room;
#endif
}

static void
GC_ClearUsers(void)
{
#ifndef DEDICATED_ONLY
	if (!gc_shutdown)
	{
		MC_ClearUsers();
	}
#endif
}

/* the users widget's list: cleared, then "\x01@nick" operator, "\x04+nick"
   voice, "\x1c nick" normal (SPEC-803 section 3); console "<n> users found" */
static void
GC_ShowUsers(const char *channel, const gclist_t *l)
{
	int i;

	GC_ClearUsers();

	for (i = 0; i < l->num; i++)
	{
		const gcmember_t *m = &l->m[i];
		const char *e;

		if (m->op)
		{
			e = va(C_OP "@%s", m->nick);
		}
		else if (m->voice)
		{
			e = va(C_STATUS "+%s", m->nick);
		}
		else
		{
			e = va(C_NORMAL " %s", m->nick);
		}

#ifndef DEDICATED_ONLY
		if (!gc_shutdown)
		{
			MC_AddUser(e);
		}
#endif
	}

	Com_Printf("%i users found in #%s\n", l->num, channel);

	for (i = 0; i < l->num; i++)
	{
		const gcmember_t *m = &l->m[i];

		Com_DPrintf("%s%s\n", m->op ? "@" : (m->voice ? "+" : " "), m->nick);
	}
}

/* menu_topic := "#<channel>: <topic>", control characters removed, at most
   63 characters (SPEC-204 5.4) */
static void
GC_SetMenuTopic(const char *channel, const char *topic)
{
	char raw[GSC_TEXTLEN], text[GSC_TEXTLEN];

	Com_sprintf(raw, sizeof(raw), "#%s: %s", channel, topic);
	GC_Clean(text, sizeof(text), raw);
	text[GSC_TOPIC_MAX] = 0;
	Cvar_Set("menu_topic", text);
}

/* ---- sending ---- */

/* requests are sent only while the session is registered, i.e. after the
   welcome (SPEC-AMEND-006 I2d.1) */
static qboolean
GC_Registered(void)
{
	return (gc_sock != GS_NOSOCK) && (gc_state == GC_ONLINE);
}

static void
GC_Send(const char *fmt, ...)
{
	char line[GSC_TEXTLEN];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(line, sizeof(line) - 2, fmt, ap);
	va_end(ap);

	if ((n < 0) || (n > (int)sizeof(line) - 3))
	{
		n = (int)strlen(line);
	}

	line[n++] = '\r';
	line[n++] = '\n';

	if (gc_outlen + n > GSC_OUTBUF)
	{
		Com_DPrintf("GameSpy chat: send buffer full, line dropped\n");
		return;
	}

	memcpy(gc_out + gc_outlen, line, n);
	gc_outlen += n;
}

/* writes as much as the socket accepts, at most 1024 bytes per write */
static void
GC_Flush(void)
{
	while ((gc_outlen > 0) && (gc_sock != GS_NOSOCK) && (gc_state != GC_CONNECTING))
	{
		int n = gc_outlen > GSC_WRITE_CHUNK ? GSC_WRITE_CHUNK : gc_outlen;
		int sent = GS_TCPSend(gc_sock, gc_out, n);

		if (sent <= 0)
		{
			return; /* would block, or the error shows up on the next read */
		}

		memmove(gc_out, gc_out + sent, gc_outlen - sent);
		gc_outlen -= sent;
	}
}

static void
GC_KeepAlive(gssock_t s)
{
	int one = 1;

#ifdef _WIN32
	setsockopt((SOCKET)s, SOL_SOCKET, SO_KEEPALIVE, (const char *)&one, sizeof(one));
#else
	setsockopt((int)s, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
#endif
}

/* ================================================================== */
/* session                                                            */
/* ================================================================== */

static void
GC_DropHeld(void)
{
	int i;

	for (i = 0; i < gc_numheld; i++)
	{
		free(gc_held[i]);
	}

	gc_numheld = 0;
}

static void
GC_CancelRequests(void)
{
	int i;

	gc_numlists = 0;
	gc_joining = false;
	GC_ListClear(&gc_joinnames);
	GC_DropHeld();

	for (i = 0; i < GSC_MAXNAMES; i++)
	{
		gc_names[i].active = false;
		GC_ListClear(&gc_names[i].names);
	}
}

/* "Disconnected <reason>" (public log and developer console); the user
   list is cleared, the current channel stays */
static void
GC_Disconnected(const char *reason)
{
	GC_Public(va(C_STATUS "Disconnected %s", reason));
	Com_DPrintf("Disconnected %s\n", reason);

	gc_inchannel = false;
	GC_ListClear(&gc_members);
	GC_ClearUsers();
}

static void
GC_CloseSocket(void)
{
	GS_Close(gc_sock);
	gc_sock = GS_NOSOCK;
	gc_outlen = 0;
	gc_inlen = 0;
}

/* gsc_disconnect, and the start of gsc_connect: QUIT only after the welcome */
static void
GC_EndSession(void)
{
	if (gc_state == GC_NONE)
	{
		return;
	}

	if ((gc_state == GC_ONLINE) && (gc_sock != GS_NOSOCK))
	{
		GC_Send("QUIT :Later!");
		GC_Flush();
	}

	GC_CloseSocket();
	GC_CancelRequests();
	gc_state = GC_NONE;
	GC_Disconnected("");
}

/* the server closed the connection */
static void
GC_ServerClosed(void)
{
	GC_CloseSocket();
	GC_CancelRequests();

	if (gc_state == GC_CONNECTING)
	{
		gc_state = GC_NONE;
		return;
	}

	gc_state = GC_CLOSED;
	GC_Disconnected("Disconnected");
}

static void
GC_ConnectFailed(void)
{
	char text[512];

	GC_CloseSocket();
	GC_CancelRequests();
	gc_state = GC_NONE;

	Com_sprintf(text, sizeof(text), C_ERROR "Failed to connect to %s:%i", gc_server, gc_port);
	Com_Printf("%s\n", text);
	GC_Public(text);
}

/* a nick may hold the letters, the digits and [ ] \ ` _ ^ { | } -, and
   must not start with a digit (SPEC-204 5.1) */
static qboolean
GC_ValidNick(const char *nick)
{
	const char *s;

	if (!nick[0] || ((nick[0] >= '0') && (nick[0] <= '9')))
	{
		return false;
	}

	for (s = nick; *s; s++)
	{
		char c = *s;

		if (((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) ||
			((c >= '0') && (c <= '9')) || strchr("[]\\`_^{|}-", c))
		{
			continue;
		}

		return false;
	}

	return true;
}

/* gsc_getchannels and the welcome: the room list is cleared even without a
   session; LIST is sent and waited for only while registered (I2d.4) */
static void
GC_RequestList(void)
{
	GC_ClearRooms();

	if (!GC_Registered())
	{
		return;
	}

	GC_Send("LIST");

	if (gc_numlists == GSC_MAXLISTS)
	{
		/* OpenMerc: more requests than answers; the oldest is forgotten */
		memmove(gc_liststart, gc_liststart + 1, (GSC_MAXLISTS - 1) * sizeof(int));
		memmove(gc_listcount, gc_listcount + 1, (GSC_MAXLISTS - 1) * sizeof(int));
		gc_numlists--;
	}

	gc_liststart[gc_numlists] = Sys_Milliseconds();
	gc_listcount[gc_numlists] = 0;
	gc_numlists++;
}

/* the oldest request ends: with the end of its list (323) it reports the
   number of 322 replies; after the 30 s limit it reports 0 even when some
   replies came (black-box, I2d) */
static void
GC_ListDone(qboolean answered)
{
	int count;

	if (!gc_numlists)
	{
		return;
	}

	count = answered ? gc_listcount[0] : 0;
	gc_numlists--;
	memmove(gc_liststart, gc_liststart + 1, gc_numlists * sizeof(int));
	memmove(gc_listcount, gc_listcount + 1, gc_numlists * sizeof(int));

	Com_Printf("%i channels found.\n", count);
}

/* ================================================================== */
/* incoming lines                                                     */
/* ================================================================== */

#define GC_MAXARGS 16

typedef struct
{
	char nick[GSC_NAMELEN];     /* prefix up to '!', or the server name */
	qboolean user;              /* the prefix names a user (has '!') */
	const char *cmd;
	char rest[GSC_TEXTLEN];     /* the line after the command, as received */
	int argc;
	char *argv[GC_MAXARGS];
	const char *raw;            /* the whole line as received */
} gcmsg_t;

static qboolean
GC_Parse(char *line, gcmsg_t *m)
{
	char *s = line;

	memset(m, 0, sizeof(*m));

	if (*s == ':')
	{
		char *p = ++s;

		while (*s && (*s != ' '))
		{
			s++;
		}

		if (*s)
		{
			*s++ = 0;
		}

		{
			char *bang = strchr(p, '!');

			m->user = bang != NULL;

			if (bang)
			{
				*bang = 0;
			}
		}

		Q_strlcpy(m->nick, p, sizeof(m->nick));
	}

	while (*s == ' ')
	{
		s++;
	}

	if (!*s)
	{
		return false;
	}

	m->cmd = s;

	while (*s && (*s != ' '))
	{
		s++;
	}

	if (*s)
	{
		*s++ = 0;
	}

	while (*s == ' ')
	{
		s++;
	}

	/* the rest is split in place below; PING answers with it unchanged */
	Q_strlcpy(m->rest, s, sizeof(m->rest));

	while (*s && (m->argc < GC_MAXARGS))
	{
		if (*s == ':')
		{
			m->argv[m->argc++] = s + 1;
			break;
		}

		m->argv[m->argc++] = s;

		while (*s && (*s != ' '))
		{
			s++;
		}

		if (*s)
		{
			*s++ = 0;
		}

		while (*s == ' ')
		{
			s++;
		}
	}

	return true;
}

static const char *
GC_Arg(const gcmsg_t *m, int i)
{
	return (i < m->argc) ? m->argv[i] : "";
}

static qboolean
GC_IsMe(const char *nick)
{
	return gc_me[0] && !Q_stricmp(nick, gc_me);
}

static qboolean GC_InCurrent(const char *channel);
static void GC_HandleLine(char *line);

/* An event or result that names a channel is delivered only while the client
   is in that channel; while its join is not complete yet it is held and
   handled again when the join completes; otherwise it is discarded
   (SPEC-AMEND-006 I2d.6). Returns true when it is to be handled now. */
static qboolean
GC_Deliver(const char *channel, const gcmsg_t *m)
{
	if (GC_IsChannel(channel) && GC_InCurrent(channel))
	{
		return true;
	}

	if (gc_joining && gc_joinecho && GC_IsChannel(channel) &&
		GC_SameChannel(channel, gc_joinchan) && (gc_numheld < GSC_MAXHELD) && m->raw)
	{
		gc_held[gc_numheld] = malloc(strlen(m->raw) + 1);

		if (gc_held[gc_numheld])
		{
			strcpy(gc_held[gc_numheld], m->raw);
			gc_numheld++;
		}
	}

	return false;
}

/* ---- registration ---- */

static void
GC_Welcome(const gcmsg_t *m)
{
	char text[512];

	if (gc_state != GC_REGISTERING)
	{
		return;
	}

	if (m->argc > 0 && m->argv[0][0])
	{
		Q_strlcpy(gc_me, m->argv[0], sizeof(gc_me));
	}

	gc_state = GC_ONLINE;

	Com_sprintf(text, sizeof(text), "Connected to %s:%i!", gc_server, gc_port);
	Com_Printf("%s\n", text);
	GC_Public(va(C_STATUS "%s", text));
	GC_RequestList();
}

/* 1.07f: an ERROR before the welcome still reports "Connected" and clears the
   room list; the session stays unregistered, so no request is sent, but the
   socket stays open and a server PING is still answered (SPEC-AMEND-006
   I2d.1) */
static void
GC_Refused(void)
{
	char text[512];

	gc_state = GC_REFUSED;

	Com_sprintf(text, sizeof(text), "Connected to %s:%i!", gc_server, gc_port);
	Com_Printf("%s\n", text);
	GC_Public(va(C_STATUS "%s", text));
	GC_ClearRooms();
}

static void
GC_NickRefused(qboolean inuse)
{
	const char *nick = gsc_nick->string;

	if (inuse)
	{
		GC_Public(va(C_ERROR "Nick %s is already in use - please change", nick));
		Com_DPrintf("Nick already in use - please change\n");
	}
	else
	{
		GC_Public(va(C_ERROR "Nick %s is invalid - please change", nick));
		Com_DPrintf("Nick is invalid - please change\n");
	}

	/* closed by the client, without QUIT */
	GC_CloseSocket();
	GC_CancelRequests();
	gc_state = GC_NONE;
	GC_Disconnected("");
}

/* ---- rooms ---- */

/* 321 and 322 go to the oldest list request and restart its 30 s; a 322
   counts only with exactly four parameters (own nick, channel, users,
   topic), and its room is added at once (SPEC-AMEND-006 I2d.4) */
static void
GC_ListReply(const gcmsg_t *m, qboolean entry)
{
	const char *name = GC_Arg(m, 1);

	if (!gc_numlists)
	{
		return; /* no request waits for it */
	}

	gc_liststart[0] = Sys_Milliseconds();

	if (!entry || (m->argc != 4))
	{
		return;
	}

	if (name[0] && (name[0] != '*'))
	{
		GC_AddRoom(name + 1);
		Com_DPrintf("Channel %i: %s - \"%s\" with %s users\n", gc_listcount[0], name,
				GC_Arg(m, 3), GC_Arg(m, 2));
	}

	gc_listcount[0]++;
}

/* ---- joins ---- */

static void
GC_JoinFailed(void)
{
	char text[256];

	gc_joining = false;
	GC_ListClear(&gc_joinnames);
	GC_DropHeld();
	gc_channel[0] = 0;
	gc_inchannel = false;

	Com_sprintf(text, sizeof(text), C_ERROR "Failed to enter/create channel #%s", gc_joinchan);
	Com_Printf("%s\n", text);
	GC_Public(text);
}

static const char *
GC_ModeNames(const char *modes)
{
	static char out[256];
	static const struct
	{
		char c;
		const char *name;
	} names[] = {
		{'i', "InviteOnly"}, {'p', "Private"}, {'s', "Secret"}, {'m', "Moderated"},
		{'n', "NoExternalMessages"}, {'t', "OnlyOpsChangeTopic"}
	};
	int i;

	out[0] = 0;

	for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++)
	{
		if (strchr(modes, names[i].c))
		{
			Q_strlcat(out, names[i].name, sizeof(out));
			Q_strlcat(out, " ", sizeof(out));
		}
	}

	return out;
}

static void
GC_JoinDone(const char *modes)
{
	char text[256], modecopy[64];

	Q_strlcpy(modecopy, modes, sizeof(modecopy));
	gc_joining = false;
	Q_strlcpy(gc_channel, gc_joinchan, sizeof(gc_channel));
	gc_inchannel = true;
	GC_ListCopy(&gc_members, &gc_joinnames);
	GC_ListClear(&gc_joinnames);

	Com_sprintf(text, sizeof(text), "Successfully entered/created channel #%s!", gc_channel);
	Com_Printf("%s\n", text);
	GC_Public(va(C_STATUS "%s", text));

	GC_ShowUsers(gc_channel, &gc_members);

	/* the channel's events that came during the join (I2d.6), in arrival
	   order; then the join's own topic and modes (black-box 2026-10-25,
	   orig_amend006: a TOPIC change held during the join is printed first,
	   the join's topic last and it is the one left in menu_topic) */
	{
		char *held[GSC_MAXHELD];
		int i, n = gc_numheld;

		memcpy(held, gc_held, n * sizeof(char *));
		gc_numheld = 0;

		for (i = 0; i < n; i++)
		{
			if (gc_sock != GS_NOSOCK)
			{
				GC_HandleLine(held[i]);
			}

			free(held[i]);
		}
	}

	if (!gc_inchannel || Q_stricmp(gc_channel, gc_joinchan))
	{
		return; /* a held event took the client out again (a kick) */
	}

	Q_strlcpy(gc_topic, gc_jointopic, sizeof(gc_topic));
	Com_DPrintf("Topic for #%s is \"%s\"\n", gc_channel, gc_topic);
	GC_SetMenuTopic(gc_channel, gc_topic);
	Com_DPrintf("Channel #%s modechange: %s\n", gc_channel, GC_ModeNames(modecopy));
}

/* ---- the user list of the current channel ---- */

static qboolean
GC_InCurrent(const char *channel)
{
	return gc_inchannel && GC_SameChannel(channel, gc_channel);
}

static void
GC_RefreshUsers(void)
{
	GC_ShowUsers(gc_channel, &gc_members);
}

static void
GC_AddNames(gclist_t *l, const char *names)
{
	char nick[GSC_NAMELEN];
	const char *s = names;

	while (*s)
	{
		qboolean op = false, voice = false;
		gcmember_t *m;
		int n = 0;

		while (*s == ' ')
		{
			s++;
		}

		if (!*s)
		{
			break;
		}

		for (; (*s == '@') || (*s == '+'); s++)
		{
			if (*s == '@')
			{
				op = true;
			}
			else
			{
				voice = true;
			}
		}

		while (*s && (*s != ' '))
		{
			if (n < GSC_NAMELEN - 1)
			{
				nick[n++] = *s;
			}

			s++;
		}

		nick[n] = 0;

		if (n && (m = GC_ListAdd(l, nick)) != NULL)
		{
			m->op = op;
			m->voice = voice;
		}
	}
}

/* the oldest pending NAMES request for the channel (I2d.4) */
static gcnames_t *
GC_FindNames(const char *channel)
{
	gcnames_t *best = NULL;
	int i;

	for (i = 0; i < GSC_MAXNAMES; i++)
	{
		if (gc_names[i].active && GC_SameChannel(channel, gc_names[i].channel) &&
			(!best || ((int)(gc_names[i].seq - best->seq) < 0)))
		{
			best = &gc_names[i];
		}
	}

	return best;
}

/* "opped", "voiced" or "peoned": the user's state after the change */
static void
GC_UserMode(const char *channel, const char *modes, const gcmsg_t *m, int nextarg)
{
	qboolean add = true;
	const char *s;

	for (s = modes; *s; s++)
	{
		const char *param = NULL;
		int i;

		switch (*s)
		{
			case '+':
				add = true;
				continue;
			case '-':
				add = false;
				continue;
			case 'o':
			case 'v':
			case 'b':
			case 'k':
			case 'e':
			case 'I':
			case 'h':
				param = GC_Arg(m, nextarg++);
				break;
			case 'l':
				if (add)
				{
					nextarg++;
				}

				continue;
			default:
				continue; /* channel flags: nothing is shown */
		}

		if (((*s != 'o') && (*s != 'v')) || !param[0] || !GC_InCurrent(channel))
		{
			continue;
		}

		i = GC_ListFind(&gc_members, param);

		if (i < 0)
		{
			continue;
		}

		if (*s == 'o')
		{
			gc_members.m[i].op = add;
		}
		else
		{
			gc_members.m[i].voice = add;
		}

		{
			const gcmember_t *u = &gc_members.m[i];
			const char *what = u->op ? "opped" : (u->voice ? "voiced" : "peoned");
			const char *text = va(C_STATUS "%s %s in #%s", u->nick, what, gc_channel);

			GC_Public(text);
			Com_DPrintf("%s\n", text + 1);
		}

		GC_RefreshUsers();
	}
}

/* a topic reply (331/332, reply) or a TOPIC change (not a reply) */
static void
GC_Topic(const char *channel, const char *topic, const gcmsg_t *m, qboolean reply)
{
	if (reply && gc_joining && gc_joinecho && GC_SameChannel(channel, gc_joinchan))
	{
		/* part of the join's answer */
		Q_strlcpy(gc_jointopic, topic, sizeof(gc_jointopic));
		gc_joinstart = Sys_Milliseconds();
		return;
	}

	/* a topic of a channel the client is not in never reaches menu_topic,
	   also as the answer of gsc_gettopic (SPEC-AMEND-006 I2d.6) */
	if (!GC_Deliver(channel, m))
	{
		return;
	}

	Q_strlcpy(gc_topic, topic, sizeof(gc_topic));
	Com_DPrintf("Topic for #%s is \"%s\"\n", gc_channel, gc_topic);
	GC_SetMenuTopic(gc_channel, gc_topic);
}

/* ---- messages ---- */

enum
{
	MSG_NORMAL,
	MSG_ACTION,
	MSG_NOTICE,
	MSG_UTM
};

static const char *
GC_Form(int type, const char *nick, const char *channel, const char *text)
{
	switch (type)
	{
		case MSG_ACTION:
			return va(C_ACTION "* %s %s", nick, text);
		case MSG_NOTICE:
			return channel ? va(C_ERROR "-%s: %s- %s", nick, channel, text) :
				va(C_ERROR "-%s: %s-", nick, text);
		default:
			return va(C_NORMAL "%s: %s", nick, text);
	}
}

/* a channel message, received or own (SPEC-204 5.5) */
static void
GC_ShowChannel(const char *nick, const char *channel, int type, const char *text)
{
	char clean[GSC_TEXTLEN];

	GC_Clean(clean, sizeof(clean), text);
	Com_DPrintf("%s: %s: %i: %s\n", channel, nick, type, clean);
	GC_Public(GC_Form(type, nick, channel, clean));
}

static void
GC_Incoming(const gcmsg_t *m, int type)
{
	char text[GSC_TEXTLEN], clean[GSC_TEXTLEN];
	const char *target = GC_Arg(m, 0);
	const char *selected;
	char form[GSC_TEXTLEN];
	int len;

	if (!m->user)
	{
		return; /* server notices */
	}

	Q_strlcpy(text, GC_Arg(m, 1), sizeof(text));
	len = (int)strlen(text);

	/* CTCP (SPEC-AMEND-006 I2d.2): a PRIVMSG text is a request only when it
	   starts with byte 1, its second character is a letter, it ends with
	   byte 1 and it holds a space after the command word. PING (exact case)
	   is answered and not shown, ACTION (exact case) is an action, any other
	   request is ignored. Every other text is an ordinary message
	   ("\x01VERSION\x01" shows as VERSION). */
	if ((type == MSG_NORMAL) && (len >= 3) && (text[0] == 1) && (text[len - 1] == 1) &&
		(((text[1] >= 'A') && (text[1] <= 'Z')) || ((text[1] >= 'a') && (text[1] <= 'z'))))
	{
		char *space;

		text[len - 1] = 0;
		space = strchr(text + 1, ' ');

		if (!space)
		{
			text[len - 1] = 1; /* not a request */
		}
		else
		{
			const char *arg = space + 1;

			*space = 0;

			if (!strcmp(text + 1, "PING"))
			{
				if (GC_Registered())
				{
					GC_Send("NOTICE %s :\x01PING %s\x01", m->nick, arg);
				}

				return;
			}

			if (strcmp(text + 1, "ACTION"))
			{
				return;
			}

			type = MSG_ACTION;
			memmove(text, arg, strlen(arg) + 1);
		}
	}

	/* not to the own nick: a message of the channel named as the target,
	   shown only while the client is in it (I2d.6) */
	if (!GC_IsMe(target))
	{
		if (GC_Deliver(target, m))
		{
			GC_ShowChannel(m->nick, target, type, text);
		}

		return;
	}

	GC_Clean(clean, sizeof(clean), text);
	Com_DPrintf("%s: %i: %s\n", m->nick, type, clean);
	Q_strlcpy(form, GC_Form(type, m->nick, NULL, clean), sizeof(form));
	selected = GC_Selected();

	if (selected[0] && !Q_stricmp(selected, m->nick))
	{
		GC_Private(form);
	}
	else
	{
		GC_Public(va(C_BRACKET "[%s" C_BRACKET "]", form));
	}
}

/* ---- dispatch ---- */

static qboolean
GC_Numeric(const gcmsg_t *m, int *num)
{
	const char *c = m->cmd;

	if ((strlen(c) != 3) || (c[0] < '0') || (c[0] > '9') || (c[1] < '0') ||
		(c[1] > '9') || (c[2] < '0') || (c[2] > '9'))
	{
		return false;
	}

	*num = atoi(c);
	return true;
}

static void
GC_HandleNumeric(const gcmsg_t *m, int num)
{
	switch (num)
	{
		case 1:
			GC_Welcome(m);
			break;

		case 432:
		case 433:
			if (gc_state == GC_REGISTERING)
			{
				GC_NickRefused(num == 433);
			}

			break;

		case 321:
		case 322:
			GC_ListReply(m, num == 322);
			break;

		case 323:
			GC_ListDone(true);
			break;

		case 331:
			GC_Topic(GC_Arg(m, 1), "", m, true);
			break;

		case 332:
			GC_Topic(GC_Arg(m, 1), GC_Arg(m, 2), m, true);
			break;

		case 353:
		{
			/* <me> [=|*|@] <#channel> :<names> */
			const char *channel = GC_Arg(m, m->argc - 2);
			const char *names = GC_Arg(m, m->argc - 1);
			gcnames_t *req;

			if (m->argc < 3)
			{
				break;
			}

			/* each matched reply restarts its request's 30 s (I2d.4) */
			if (gc_joining && gc_joinecho && GC_SameChannel(channel, gc_joinchan))
			{
				GC_AddNames(&gc_joinnames, names);
				gc_joinstart = Sys_Milliseconds();
			}
			else if ((req = GC_FindNames(channel)) != NULL)
			{
				GC_AddNames(&req->names, names);
				req->start = Sys_Milliseconds();
			}

			break;
		}

		case 366:
		{
			/* only an end of names with exactly three parameters ends a NAMES
			   request; no error numeric does (SPEC-AMEND-006 I2d.5) */
			gcnames_t *req = GC_FindNames(GC_Arg(m, 1));

			if (gc_joining && gc_joinecho && GC_SameChannel(GC_Arg(m, 1), gc_joinchan))
			{
				gc_joinstart = Sys_Milliseconds();
			}
			else if (req && (m->argc == 3))
			{
				req->active = false;
				GC_ShowUsers(req->channel, &req->names);
				GC_ListClear(&req->names);
			}

			break;
		}

		case 324:
			if (gc_joining && gc_joinecho && GC_SameChannel(GC_Arg(m, 1), gc_joinchan))
			{
				GC_JoinDone(GC_Arg(m, 2));
			}
			else
			{
				Com_DPrintf("Channel %s modechange: %s\n", GC_Arg(m, 1), GC_ModeNames(GC_Arg(m, 2)));
			}

			break;

		case 403:
		case 405:
		case 471:
		case 473:
		case 474:
		case 475:
		case 476:
			if (gc_joining && GC_SameChannel(GC_Arg(m, 1), gc_joinchan))
			{
				GC_JoinFailed();
			}

			break;

		default:
			/* 311 (gsc_ban's WHOIS): nothing further (I2d.3); other replies
			   are not used */
			break;
	}
}

static void
GC_HandleLine(char *line)
{
	char raw[GSC_INBUF];
	gcmsg_t m;
	int num;
	const char *cmd;

	/* the line as received, for holding it back during a join (I2d.6) */
	Q_strlcpy(raw, line, sizeof(raw));

	if (!GC_Parse(line, &m))
	{
		return;
	}

	m.raw = raw;
	cmd = m.cmd;

	/* answered in every state, also after an ERROR before the welcome
	   (SPEC-AMEND-006 I2d.1) */
	if (!Q_stricmp(cmd, "PING"))
	{
		GC_Send("PONG %s", m.rest);
		return;
	}

	if (GC_Numeric(&m, &num))
	{
		GC_HandleNumeric(&m, num);
		return;
	}

	if (!Q_stricmp(cmd, "ERROR"))
	{
		if (gc_state == GC_REGISTERING)
		{
			GC_Refused();
		}
		else if (gc_state == GC_ONLINE)
		{
			/* the server closes the connection next, which is reported too */
			gc_state = GC_CLOSED;
			GC_CancelRequests();
			GC_Disconnected(GC_Arg(&m, 0));
		}

		return;
	}

	if (gc_state != GC_ONLINE)
	{
		return;
	}

	if (!Q_stricmp(cmd, "PRIVMSG"))
	{
		GC_Incoming(&m, MSG_NORMAL);
	}
	else if (!Q_stricmp(cmd, "NOTICE"))
	{
		GC_Incoming(&m, MSG_NOTICE);
	}
	else if (!Q_stricmp(cmd, "UTM"))
	{
		GC_Incoming(&m, MSG_UTM);
	}
	else if (!Q_stricmp(cmd, "JOIN"))
	{
		const char *channel = GC_Arg(&m, 0);

		if (GC_IsMe(m.nick))
		{
			if (gc_joining && !gc_joinecho && GC_SameChannel(channel, gc_joinchan))
			{
				gc_joinecho = true;
				gc_joinstart = Sys_Milliseconds();
				GC_ListClear(&gc_joinnames);
				gc_jointopic[0] = 0;
				GC_Send("MODE %s", channel);
			}
		}
		else if (gc_joining && gc_joinecho && GC_SameChannel(channel, gc_joinchan))
		{
			/* while the join completes another user's JOIN only goes into the
			   join's user list, without a line (black-box 2026-10-25,
			   orig_amend006: "6 users found" at the join, no "joined" line) */
			GC_ListAdd(&gc_joinnames, m.nick);
		}
		else if (GC_Deliver(channel, &m))
		{
			const char *text = va(C_STATUS "%s joined channel #%s", m.nick, gc_channel);

			GC_ListAdd(&gc_members, m.nick);
			GC_Public(text);
			Com_DPrintf("%s\n", text + 1);
			GC_RefreshUsers();
		}
	}
	else if (!Q_stricmp(cmd, "PART"))
	{
		const char *channel = GC_Arg(&m, 0);

		if (!GC_IsMe(m.nick) && gc_joining && gc_joinecho && GC_SameChannel(channel, gc_joinchan))
		{
			/* OpenMerc: like a JOIN during the join (not observed) */
			GC_ListRemove(&gc_joinnames, GC_ListFind(&gc_joinnames, m.nick));
		}
		else if (!GC_IsMe(m.nick) && GC_Deliver(channel, &m))
		{
			const char *text = va(C_STATUS "%s left channel #%s", m.nick, gc_channel);

			GC_ListRemove(&gc_members, GC_ListFind(&gc_members, m.nick));
			GC_Public(text);
			Com_DPrintf("%s\n", text + 1);
			GC_RefreshUsers();
		}
	}
	else if (!Q_stricmp(cmd, "QUIT"))
	{
		int i = gc_inchannel ? GC_ListFind(&gc_members, m.nick) : -1;

		if (!GC_IsMe(m.nick) && (i >= 0))
		{
			const char *text = va(C_STATUS "%s left channel #%s", m.nick, gc_channel);

			GC_ListRemove(&gc_members, i);
			GC_Public(text);
			Com_DPrintf("%s\n", text + 1);
			GC_RefreshUsers();
		}
	}
	else if (!Q_stricmp(cmd, "KICK"))
	{
		const char *channel = GC_Arg(&m, 0);
		const char *victim = GC_Arg(&m, 1);

		if (!GC_IsMe(victim) && gc_joining && gc_joinecho && GC_SameChannel(channel, gc_joinchan))
		{
			/* OpenMerc: like a PART during the join (not observed) */
			GC_ListRemove(&gc_joinnames, GC_ListFind(&gc_joinnames, victim));
		}
		else if (!GC_Deliver(channel, &m))
		{
			/* not in that channel (I2d.6) */
		}
		else if (GC_IsMe(victim))
		{
			/* 1.07f names the kicker; the current channel is kept (a later
			   gsc_join sends PART for it), the user list stays */
			const char *text = va(C_STATUS "%s was kicked from %s because %s", m.nick,
					channel, GC_Arg(&m, 2));

			GC_Public(text);
			Com_DPrintf("%s\n", text + 1);
			gc_inchannel = false;
			GC_ListClear(&gc_members);
		}
		else
		{
			const char *text = va(C_STATUS "%s left channel #%s", victim, gc_channel);

			GC_ListRemove(&gc_members, GC_ListFind(&gc_members, victim));
			GC_Public(text);
			Com_DPrintf("%s\n", text + 1);
			GC_RefreshUsers();
		}
	}
	else if (!Q_stricmp(cmd, "NICK"))
	{
		const char *newnick = GC_Arg(&m, 0);
		const char *text = va(C_STATUS "%s is now known as %s", m.nick, newnick);
		int i;

		GC_Public(text);
		Com_DPrintf("%s\n", text + 1);

		/* 1.07f sets gsc_nick for every user's change; OpenMerc only for
		   its own (docs/DEVIATIONS.md) */
		if (GC_IsMe(m.nick))
		{
			Q_strlcpy(gc_me, newnick, sizeof(gc_me));
			Cvar_Set("gsc_nick", newnick);
		}

		if (gc_inchannel && ((i = GC_ListFind(&gc_members, m.nick)) >= 0))
		{
			Q_strlcpy(gc_members.m[i].nick, newnick, GSC_NAMELEN);
			GC_RefreshUsers();
		}
	}
	else if (!Q_stricmp(cmd, "MODE"))
	{
		const char *target = GC_Arg(&m, 0);

		if (GC_IsChannel(target) && GC_Deliver(target, &m))
		{
			GC_UserMode(target, GC_Arg(&m, 1), &m, 2);
		}
	}
	else if (!Q_stricmp(cmd, "TOPIC"))
	{
		GC_Topic(GC_Arg(&m, 0), GC_Arg(&m, 1), &m, false);
	}
}

static void
GC_Read(void)
{
	while (gc_sock != GS_NOSOCK)
	{
		int n = GS_TCPRecv(gc_sock, gc_in + gc_inlen, GSC_INBUF - 1 - gc_inlen);
		int start, i;

		if (n == 0)
		{
			return;
		}

		if (n < 0)
		{
			GC_ServerClosed();
			return;
		}

		gc_inlen += n;

		/* lines end with CR, LF or both */
		for (start = 0, i = 0; (i < gc_inlen) && (gc_sock != GS_NOSOCK); i++)
		{
			if ((gc_in[i] == '\r') || (gc_in[i] == '\n'))
			{
				gc_in[i] = 0;

				if (i > start)
				{
					GC_HandleLine(gc_in + start);
				}

				start = i + 1;
			}
		}

		if (gc_sock == GS_NOSOCK)
		{
			return; /* the line handler ended the session */
		}

		if (start > 0)
		{
			memmove(gc_in, gc_in + start, gc_inlen - start);
			gc_inlen -= start;
		}
		else if (gc_inlen >= GSC_INBUF - 1)
		{
			/* an overlong line: handled as far as it fits */
			gc_in[gc_inlen] = 0;
			GC_HandleLine(gc_in);
			gc_inlen = 0;
		}
	}
}

/* ================================================================== */
/* commands (SPEC-204 5.1-5.4)                                        */
/* ================================================================== */

static void
GSC_Connect_f(void)
{
	static const char *const names[] = {"gsc_server", "gsc_port", "gsc_nick", "gsc_user"};
	const char *nick;
	gsaddr_t addr;
	int i;

	GC_EndSession();

	for (i = 1; (i < Cmd_Argc()) && (i <= 4); i++)
	{
		Cvar_Set(names[i - 1], Cmd_Argv(i));
	}

	Q_strlcpy(gc_server, gsc_server->string, sizeof(gc_server));
	gc_port = atoi(gsc_port->string);

	Com_Printf("Connecting to " C_HILITE "%s:%i" C_OP " with nickname " C_HILITE "%s"
			C_OP " and username " C_HILITE "%s" C_OP "\n", gc_server, gc_port,
			gsc_nick->string, gsc_user->string);
	GC_Public(va(C_STATUS "Connecting to: %s", gc_server));

	gc_outlen = gc_inlen = 0;

	if ((gc_port <= 0) || (gc_port > 65535) || !GS_Resolve(gc_server, gc_port, &addr) ||
		((gc_sock = GS_TCPConnect(&addr)) == GS_NOSOCK))
	{
		GC_ConnectFailed();
		return;
	}

	gc_state = GC_CONNECTING;
	gc_connectstart = Sys_Milliseconds();

	/* registration; sent once the connection is up */
	nick = GC_ValidNick(gsc_nick->string) ? gsc_nick->string : "1";
	Q_strlcpy(gc_me, nick, sizeof(gc_me));
	GC_Send("NICK %s", nick);
	GC_Send("USER %s 127.0.0.1 %s :%s", gsc_user->string, gc_server, GSC_REALNAME);
}

static void
GSC_Disconnect_f(void)
{
	GC_EndSession();
}

/* clears the room list also without a session (SPEC-AMEND-006 I2d.4) */
static void
GSC_GetChannels_f(void)
{
	GC_RequestList();
}

static void
GSC_Join_f(void)
{
	const char *channel;
	qboolean registered;

	if (Cmd_Argc() < 2)
	{
		Com_Printf("Usage: gsc_join <channel> [password]\n");
		return;
	}

	if (gc_sock == GS_NOSOCK)
	{
		return; /* no chat connection */
	}

	/* 1.07f on a connection that is not registered (ERROR before the
	   welcome): the log lines appear, nothing is sent, nothing waits, the
	   current channel stays (black-box 2026-10-25, orig_amend006) */
	registered = GC_Registered();
	channel = Cmd_Argv(1);

	if (gc_channel[0] && !Q_stricmp(channel, gc_channel))
	{
		GC_Public(va(C_STATUS "Already in channel: #%s!", gc_channel));
		return;
	}

	if (gc_channel[0])
	{
		GC_Public(va(C_STATUS "Leaving channel: #%s", gc_channel));

		if (registered)
		{
			GC_Send("PART #%s", gc_channel);
		}

		gc_inchannel = false;
		GC_ListClear(&gc_members);
	}

	GC_Public(va(C_STATUS "Joining channel: #%s", channel));

	if (!registered)
	{
		return;
	}

	GC_Send("JOIN #%s %s", channel, Cmd_Argc() > 2 ? Cmd_Argv(2) : "");

	Q_strlcpy(gc_joinchan, channel, sizeof(gc_joinchan));
	gc_joining = true;
	gc_joinecho = false;
	gc_joinstart = Sys_Milliseconds();
	GC_ListClear(&gc_joinnames);
	gc_jointopic[0] = 0;
}

static void
GSC_Part_f(void)
{
	const char *channel;

	if (Cmd_Argc() < 2)
	{
		Com_Printf("Usage: gsc_part <channelname>\n");
		return;
	}

	if (!GC_Registered())
	{
		return;
	}

	channel = Cmd_Argv(1);
	GC_Send("PART #%s", channel);

	if (gc_channel[0] && !Q_stricmp(channel, gc_channel))
	{
		gc_inchannel = false; /* the current channel itself is kept */
		GC_ListClear(&gc_members);
	}

	GC_ClearUsers();
	GC_Public(va(C_STATUS "Left channel: #%s", channel));
}

/* "/me " and "/notice " (any case) select an action or a notice */
static int
GC_TextType(const char **text)
{
	if (!Q_strncasecmp(*text, "/me ", 4))
	{
		*text += 4;
		return MSG_ACTION;
	}

	if (!Q_strncasecmp(*text, "/notice ", 8))
	{
		*text += 8;
		return MSG_NOTICE;
	}

	return MSG_NORMAL;
}

static void
GC_SendText(const char *target, int type, const char *text)
{
	if (type == MSG_ACTION)
	{
		GC_Send("PRIVMSG %s :\x01" "ACTION %s\x01", target, text);
	}
	else if (type == MSG_NOTICE)
	{
		GC_Send("NOTICE %s :%s", target, text);
	}
	else
	{
		GC_Send("PRIVMSG %s :%s", target, text);
	}
}

static void
GSC_Talk_f(void)
{
	char target[GSC_NAMELEN + 1];
	const char *text;
	int type;

	if ((Cmd_Argc() < 3) || !GC_Registered())
	{
		return;
	}

	text = Cmd_Argv(2);

	if (!text[0])
	{
		return;
	}

	Com_sprintf(target, sizeof(target), "#%s", Cmd_Argv(1));
	type = GC_TextType(&text);
	GC_SendText(target, type, text);

	/* shown like an incoming message from the own nick, so under the same
	   rule: only while the client is in that channel (black-box 2026-10-25,
	   orig_fake2 "ev_close": no echo for a channel the client has not
	   joined; SPEC-AMEND-006 I2d.6) - held while its join completes */
	if (GC_InCurrent(target))
	{
		GC_ShowChannel(gc_me, target, type, text);
	}
	else
	{
		gcmsg_t m;
		char line[GSC_TEXTLEN + 2 * GSC_NAMELEN + 32];

		memset(&m, 0, sizeof(m));
		Com_sprintf(line, sizeof(line), ":%s!me@local %s %s :%s%s%s", gc_me,
				type == MSG_NOTICE ? "NOTICE" : "PRIVMSG", target,
				type == MSG_ACTION ? "\x01" "ACTION " : "", text, type == MSG_ACTION ? "\x01" : "");
		m.raw = line;
		GC_Deliver(target, &m);
	}
}

static void
GSC_Private_f(void)
{
	char clean[GSC_TEXTLEN];
	const char *text;
	int type;

	if ((Cmd_Argc() < 3) || !GC_Registered())
	{
		return;
	}

	text = Cmd_Argv(2);

	if (!text[0])
	{
		return;
	}

	type = GC_TextType(&text);
	GC_SendText(Cmd_Argv(1), type, text);

	GC_Clean(clean, sizeof(clean), text);
	GC_Private(GC_Form(type, gc_me, NULL, clean));
}

static void
GSC_GetUsers_f(void)
{
	const char *channel;
	int i;

	if (Cmd_Argc() < 2)
	{
		Com_Printf("Usage: gsc_getusers <channel>\n");
		return;
	}

	if (!GC_Registered())
	{
		return;
	}

	channel = Cmd_Argv(1);

	if (gc_inchannel && !Q_stricmp(channel, gc_channel))
	{
		GC_ShowUsers(channel, &gc_members); /* the known list */
		return;
	}

	GC_Send("NAMES #%s", channel);

	for (i = 0; i < GSC_MAXNAMES; i++)
	{
		if (!gc_names[i].active)
		{
			gc_names[i].active = true;
			Q_strlcpy(gc_names[i].channel, channel, GSC_NAMELEN);
			gc_names[i].start = Sys_Milliseconds();
			gc_names[i].seq = gc_namesseq++;
			GC_ListClear(&gc_names[i].names);
			break;
		}
	}
}

static void
GSC_SetTopic_f(void)
{
	if (Cmd_Argc() < 3)
	{
		Com_Printf("Usage: gsc_settopic <channel> <topic>\n");
		return;
	}

	if (GC_Registered())
	{
		GC_Send("TOPIC #%s :%s", Cmd_Argv(1), Cmd_Argv(2));
	}
}

static void
GSC_GetTopic_f(void)
{
	const char *channel;

	if (Cmd_Argc() < 2)
	{
		Com_Printf("Usage: gsc_gettopic <channel>\n");
		return;
	}

	if (!GC_Registered())
	{
		return;
	}

	channel = Cmd_Argv(1);

	if (gc_inchannel && !Q_stricmp(channel, gc_channel))
	{
		Com_DPrintf("Topic for #%s:%s\n", channel, gc_topic);
		GC_SetMenuTopic(gc_channel, gc_topic);
		return;
	}

	GC_Send("TOPIC #%s", channel);
}

static void
GSC_Kick_f(void)
{
	if (Cmd_Argc() < 4)
	{
		Com_Printf("Usage: gsc_kick <channel> <user> [reason]\n");
		return;
	}

	if (GC_Registered())
	{
		GC_Send("KICK #%s %s :%s", Cmd_Argv(1), Cmd_Argv(2), Cmd_Argv(3));
	}
}

/* only the WHOIS is ever sent: in 1.07f the ban that was meant to follow the
   311 reply is never sent, and the request expires silently
   (SPEC-AMEND-006 I2d.3) */
static void
GSC_Ban_f(void)
{
	if (Cmd_Argc() < 3)
	{
		Com_Printf("Usage: gsc_ban <channel> <user>\n");
		return;
	}

	if (GC_Registered())
	{
		GC_Send("WHOIS %s", Cmd_Argv(2));
	}
}

static void
GSC_SetUserMode_f(void)
{
	const char *channel, *user, *mode;

	if (Cmd_Argc() < 4)
	{
		Com_Printf("Usage: gsc_setusermode <channel> <user> <mode>\n");
		return;
	}

	if (!GC_Registered())
	{
		return;
	}

	channel = Cmd_Argv(1);
	user = Cmd_Argv(2);
	mode = Cmd_Argv(3);

	if (!Q_stricmp(mode, "op"))
	{
		GC_Send("MODE #%s +o %s", channel, user);
	}
	else if (!Q_stricmp(mode, "voice"))
	{
		GC_Send("MODE #%s +v %s", channel, user);
	}
	else
	{
		GC_Send("MODE #%s -o %s", channel, user);
		GC_Send("MODE #%s -v %s", channel, user);
	}
}

/* ================================================================== */
/* engine interface                                                   */
/* ================================================================== */

void
GSC_Init(void)
{
	gsc_server = Cvar_Get("gsc_server", GSC_DEFAULT_SERVER, CVAR_ARCHIVE);
	gsc_port = Cvar_Get("gsc_port", "6667", CVAR_ARCHIVE);
	gsc_nick = Cvar_Get("gsc_nick", "JohnMullins", CVAR_ARCHIVE);
	gsc_user = Cvar_Get("gsc_user", "JohnMullins", CVAR_ARCHIVE);

	/* the host of the original default no longer exists (also when an
	   original config.cfg stored it) */
	if (!Q_stricmp(gsc_server->string, GSC_ORIGINAL_SERVER))
	{
		Cvar_Set("gsc_server", GSC_DEFAULT_SERVER);
	}

	Cmd_AddCommand("gsc_connect", GSC_Connect_f);
	Cmd_AddCommand("gsc_disconnect", GSC_Disconnect_f);
	Cmd_AddCommand("gsc_getchannels", GSC_GetChannels_f);
	Cmd_AddCommand("gsc_join", GSC_Join_f);
	Cmd_AddCommand("gsc_part", GSC_Part_f);
	Cmd_AddCommand("gsc_talk", GSC_Talk_f);
	Cmd_AddCommand("gsc_private", GSC_Private_f);
	Cmd_AddCommand("gsc_getusers", GSC_GetUsers_f);
	Cmd_AddCommand("gsc_settopic", GSC_SetTopic_f);
	Cmd_AddCommand("gsc_gettopic", GSC_GetTopic_f);
	Cmd_AddCommand("gsc_kick", GSC_Kick_f);
	Cmd_AddCommand("gsc_ban", GSC_Ban_f);
	Cmd_AddCommand("gsc_setusermode", GSC_SetUserMode_f);
}

void
GSC_Frame(void)
{
	int now, i;

	if (gc_state == GC_NONE)
	{
		return;
	}

	now = Sys_Milliseconds();

	if (gc_state == GC_CONNECTING)
	{
		int r = GS_TCPConnected(gc_sock);

		if ((r < 0) || ((r == 0) && (now - gc_connectstart >= GSC_CONNECT_MSEC)))
		{
			GC_ConnectFailed();
			return;
		}

		if (r == 0)
		{
			return;
		}

		GC_KeepAlive(gc_sock);
		gc_state = GC_REGISTERING;
	}

	GC_Read();

	/* requests that wait for an answer give up after 30 s */
	while (gc_numlists && (now - gc_liststart[0] >= GSC_REQUEST_MSEC))
	{
		GC_ListDone(false);
	}

	if (gc_joining && (now - gc_joinstart >= GSC_REQUEST_MSEC))
	{
		GC_JoinFailed();
	}

	for (i = 0; i < GSC_MAXNAMES; i++)
	{
		if (gc_names[i].active && (now - gc_names[i].start >= GSC_REQUEST_MSEC))
		{
			/* 1.07f prints the line without the channel name (black-box, I2d) */
			gc_names[i].active = false;
			GC_ListClear(&gc_names[i].names);
			Com_Printf("Failed to list users for \n");
		}
	}

	GC_Flush();
}

void
GSC_Shutdown(void)
{
	/* the menus are gone by now: console only */
	gc_shutdown = true;

	if (gc_state != GC_NONE)
	{
		GC_EndSession();
	}

	GC_ListClear(&gc_members);
}
