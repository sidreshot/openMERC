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
 * OpenMerc: GameSpy (v0) protocol support shared by the server's query
 * responder and heartbeats (server/sv_gamespy.c) and the client's server
 * browser (client/cl_serverlist.c): identity constants, the validate
 * algorithm, key/value parsing and a small portable socket layer that is
 * independent of the engine's game sockets.
 *
 * Implements: SPEC-204, SPEC-200
 *
 * =======================================================================
 */

#ifndef CO_GAMESPY_H
#define CO_GAMESPY_H

#include <stdint.h>

/* SPEC-204 "Identity constants" */
#define GS_GAMENAME "sofretail"
#define GS_SECRETKEY "iVn3a3"
#define GS_GAMEVER "1.07f"       /* \basic\ reply of a server */
#define GS_SDKVER "1.6"          /* gamever the browser sends to a master */
#define GS_PORT_QUERY 28911      /* default of cvar gamespyport */
#define GS_PORT_HEARTBEAT 27900  /* master, UDP */
#define GS_PORT_LIST 28900       /* master, TCP */
#define GS_PORT_GAME 28910       /* listed server without hostport */
#define GS_HEARTBEAT_MSEC 150000

/* OpenMerc: GameSpy's own master is gone; these community masters speak the
   same protocol. Default of cvars sv_masters and cl_masters. */
#define GS_DEFAULT_MASTERS \
	"sof1master.sofvault.org master.333networks.com master.openspy.net"

/* socket handle of the GameSpy layer (SOCKET on Windows, fd elsewhere) */
typedef intptr_t gssock_t;
#define GS_NOSOCK ((gssock_t)-1)

/* IPv4 address; port in host byte order */
typedef struct
{
	unsigned char ip[4];
	unsigned short port;
} gsaddr_t;

/* validate(challenge) with the secret key; writes 4*ceil(len/3) characters
   and a NUL into out (needs outsize >= 4*ceil(len/3)+1, else truncated) */
void GS_Validate(const unsigned char *challenge, int len, const char *key,
		char *out, int outsize);

/* "host", "host:port" or "a.b.c.d[:port]" -> address (blocking DNS lookup).
   Returns 0 on failure. */
int GS_Resolve(const char *name, int defport, gsaddr_t *out);
const char *GS_AdrToString(const gsaddr_t *a);
int GS_AdrEqual(const gsaddr_t *a, const gsaddr_t *b);

/* Next \key\value pair of a key/value string; returns the position after the
   pair, or NULL at the end. Key and value are truncated to their buffers. */
const char *GS_NextPair(const char *s, char *key, int keysize,
		char *value, int valuesize);

/* UDP, non-blocking, bound to all interfaces. port 0 = any free port.
   On success *boundport receives the local port. */
gssock_t GS_UDPOpen(int port, int *boundport);
int GS_UDPSend(gssock_t s, const gsaddr_t *to, const void *data, int len);
/* >= 0: datagram length; -2: a datagram was discarded (longer than bufsize,
   or a transient error such as an ICMP port-unreachable report); -1: nothing
   (more) to read */
int GS_UDPRecv(gssock_t s, gsaddr_t *from, void *buf, int bufsize);

/* TCP, non-blocking */
gssock_t GS_TCPConnect(const gsaddr_t *to);
int GS_TCPConnected(gssock_t s);        /* 1 connected, 0 pending, -1 failed */
int GS_TCPSend(gssock_t s, const void *data, int len); /* bytes sent or -1 */
int GS_TCPRecv(gssock_t s, void *buf, int bufsize);    /* > 0 bytes, 0 none yet, -1 closed or error */

void GS_Close(gssock_t s);

/* server side (server/sv_gamespy.c) */
void SV_GameSpy_Init(void);
void SV_GameSpy_Frame(void);
void SV_GameSpy_CheckHeartbeat(void);
void SV_GameSpy_Shutdown(void);
gssock_t SV_GameSpy_Socket(void);
/* the game module's game-type name (SPEC-001 export 0x50), when one is wired */
void SV_GameSpy_SetGameTypeFunc(const char *(*func)(void));

#endif
