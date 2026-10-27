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
 * OpenMerc: GameSpy helpers shared by server and client - the validate
 * algorithm (SPEC-204 section 1), key/value parsing and a small IPv4
 * socket layer. These sockets are separate from the engine's game sockets
 * (backends/<os>/network.c): the query socket has its own port and the
 * master list uses TCP.
 *
 * Implements: SPEC-204
 *
 * =======================================================================
 */

#ifdef _WIN32
 #include <winsock2.h>
 #include <ws2tcpip.h>
 typedef int socklen_t_gs;
 #define GS_SOCK(s) ((SOCKET)(s))
#else
 #include <sys/types.h>
 #include <sys/socket.h>
 #include <sys/select.h>
 #include <sys/uio.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>
 #include <netdb.h>
 #include <fcntl.h>
 #include <unistd.h>
 #include <errno.h>
 typedef socklen_t socklen_t_gs;
 #define GS_SOCK(s) ((int)(s))
 #ifndef MSG_NOSIGNAL
  #define MSG_NOSIGNAL 0
 #endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "header/gamespy.h"

/* ================================================================== */
/* validate (SPEC-204 section 1)                                       */
/* ================================================================== */

static char
GS_Base64Char(int v)
{
	static const char alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	return alphabet[v & 63];
}

void
GS_Validate(const unsigned char *challenge, int len, const char *key,
		char *out, int outsize)
{
	unsigned char s[256];
	unsigned char scrambled[132];
	unsigned char t;
	int keylen, i, j, x, y, n, o;

	if (outsize < 1)
	{
		return;
	}

	out[0] = 0;
	keylen = (int)strlen(key);

	if ((len < 1) || (keylen < 1))
	{
		return;
	}

	if (len > 128)
	{
		len = 128; /* the responder never validates longer challenges */
	}

	/* step A: key schedule */
	for (i = 0; i < 256; i++)
	{
		s[i] = (unsigned char)i;
	}

	for (i = 0, j = 0; i < 256; i++)
	{
		j = (j + s[i] + (unsigned char)key[i % keylen]) & 255;
		t = s[i];
		s[i] = s[j];
		s[j] = t;
	}

	/* step B: scramble; the challenge byte itself advances x */
	for (i = 0, x = 0, y = 0; i < len; i++)
	{
		x = (x + challenge[i] + 1) & 255;
		y = (y + s[x]) & 255;
		t = s[x];
		s[x] = s[y];
		s[y] = t;
		scrambled[i] = challenge[i] ^ s[(s[x] + s[y]) & 255];
	}

	n = len;

	while (n % 3)
	{
		scrambled[n++] = 0;
	}

	/* step C: base 64 without padding characters */
	for (i = 0, o = 0; (i < n) && (o + 4 < outsize); i += 3)
	{
		out[o++] = GS_Base64Char(scrambled[i] >> 2);
		out[o++] = GS_Base64Char(((scrambled[i] & 3) << 4) | (scrambled[i + 1] >> 4));
		out[o++] = GS_Base64Char(((scrambled[i + 1] & 15) << 2) | (scrambled[i + 2] >> 6));
		out[o++] = GS_Base64Char(scrambled[i + 2] & 63);
	}

	out[o] = 0;
}

/* ================================================================== */
/* addresses and key/value strings                                    */
/* ================================================================== */

int
GS_Resolve(const char *name, int defport, gsaddr_t *out)
{
	char host[256];
	const char *colon;
	struct addrinfo hints, *res = NULL;
	int port = defport;
	size_t hostlen;

	if (!name || !name[0])
	{
		return 0;
	}

	colon = strrchr(name, ':');
	hostlen = colon ? (size_t)(colon - name) : strlen(name);

	if ((hostlen == 0) || (hostlen >= sizeof(host)))
	{
		return 0;
	}

	memcpy(host, name, hostlen);
	host[hostlen] = 0;

	if (colon)
	{
		port = atoi(colon + 1);

		if ((port <= 0) || (port > 65535))
		{
			return 0;
		}
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	if ((getaddrinfo(host, NULL, &hints, &res) != 0) || !res)
	{
		return 0;
	}

	memcpy(out->ip, &((struct sockaddr_in *)res->ai_addr)->sin_addr, 4);
	out->port = (unsigned short)port;
	freeaddrinfo(res);

	return 1;
}

const char *
GS_AdrToString(const gsaddr_t *a)
{
	static char buf[4][32];
	static int index;
	char *s = buf[index++ & 3];

	snprintf(s, 32, "%u.%u.%u.%u:%u", a->ip[0], a->ip[1], a->ip[2],
			a->ip[3], a->port);

	return s;
}

int
GS_AdrEqual(const gsaddr_t *a, const gsaddr_t *b)
{
	return (memcmp(a->ip, b->ip, 4) == 0) && (a->port == b->port);
}

const char *
GS_NextPair(const char *s, char *key, int keysize, char *value, int valuesize)
{
	int n;

	if (!s || (*s != '\\'))
	{
		return NULL;
	}

	s++;

	for (n = 0; *s && (*s != '\\'); s++)
	{
		if (n < keysize - 1)
		{
			key[n++] = *s;
		}
	}

	key[n] = 0;

	if (*s == '\\')
	{
		s++;
	}

	for (n = 0; *s && (*s != '\\'); s++)
	{
		if (n < valuesize - 1)
		{
			value[n++] = *s;
		}
	}

	value[n] = 0;

	return s;
}

/* ================================================================== */
/* sockets                                                            */
/* ================================================================== */

static void
GS_ToSockaddr(const gsaddr_t *a, struct sockaddr_in *s)
{
	memset(s, 0, sizeof(*s));
	s->sin_family = AF_INET;
	memcpy(&s->sin_addr, a->ip, 4);
	s->sin_port = htons(a->port);
}

static void
GS_FromSockaddr(const struct sockaddr_in *s, gsaddr_t *a)
{
	memcpy(a->ip, &s->sin_addr, 4);
	a->port = ntohs(s->sin_port);
}

static int
GS_SetNonBlocking(gssock_t s)
{
#ifdef _WIN32
	u_long one = 1;

	return ioctlsocket(GS_SOCK(s), FIONBIO, &one) == 0;
#else
	int flags = fcntl(GS_SOCK(s), F_GETFL, 0);

	return (flags != -1) && (fcntl(GS_SOCK(s), F_SETFL, flags | O_NONBLOCK) != -1);
#endif
}

static int
GS_WouldBlock(void)
{
#ifdef _WIN32
	int e = WSAGetLastError();

	return (e == WSAEWOULDBLOCK) || (e == WSAEINPROGRESS) || (e == WSAEALREADY);
#else
	return (errno == EWOULDBLOCK) || (errno == EAGAIN) || (errno == EINPROGRESS) ||
		(errno == EINTR);
#endif
}

static gssock_t
GS_NewSocket(int type, int protocol)
{
#ifdef _WIN32
	SOCKET s = socket(AF_INET, type, protocol);

	if (s == INVALID_SOCKET)
	{
		return GS_NOSOCK;
	}
#else
	int s = socket(AF_INET, type, protocol);

	if (s < 0)
	{
		return GS_NOSOCK;
	}
#endif

	if (!GS_SetNonBlocking((gssock_t)s))
	{
		GS_Close((gssock_t)s);
		return GS_NOSOCK;
	}

	return (gssock_t)s;
}

void
GS_Close(gssock_t s)
{
	if (s == GS_NOSOCK)
	{
		return;
	}

#ifdef _WIN32
	closesocket(GS_SOCK(s));
#else
	close(GS_SOCK(s));
#endif
}

gssock_t
GS_UDPOpen(int port, int *boundport)
{
	struct sockaddr_in addr;
	socklen_t_gs addrlen = sizeof(addr);
	gssock_t s = GS_NewSocket(SOCK_DGRAM, IPPROTO_UDP);

	if (s == GS_NOSOCK)
	{
		return GS_NOSOCK;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons((unsigned short)port);

	if (bind(GS_SOCK(s), (struct sockaddr *)&addr, sizeof(addr)) != 0)
	{
		GS_Close(s);
		return GS_NOSOCK;
	}

	if (boundport)
	{
		*boundport = port;

		if (getsockname(GS_SOCK(s), (struct sockaddr *)&addr, &addrlen) == 0)
		{
			*boundport = ntohs(addr.sin_port);
		}
	}

	return s;
}

int
GS_UDPSend(gssock_t s, const gsaddr_t *to, const void *data, int len)
{
	struct sockaddr_in addr;

	if (s == GS_NOSOCK)
	{
		return -1;
	}

	GS_ToSockaddr(to, &addr);

	return (int)sendto(GS_SOCK(s), (const char *)data, len, 0,
			(struct sockaddr *)&addr, sizeof(addr));
}

int
GS_UDPRecv(gssock_t s, gsaddr_t *from, void *buf, int bufsize)
{
	struct sockaddr_in addr;

	if (s == GS_NOSOCK)
	{
		return -1;
	}

#ifdef _WIN32
	{
		int addrlen = sizeof(addr);
		int ret = recvfrom(GS_SOCK(s), (char *)buf, bufsize, 0,
				(struct sockaddr *)&addr, &addrlen);

		if (ret == SOCKET_ERROR)
		{
			int e = WSAGetLastError();

			/* a longer datagram is discarded by Windows (SPEC-204 section 4);
			   WSAECONNRESET reports an ICMP "port unreachable" for an earlier send */
			if ((e == WSAEMSGSIZE) || (e == WSAECONNRESET))
			{
				return -2;
			}

			return -1;
		}

		GS_FromSockaddr(&addr, from);
		return ret;
	}
#else
	{
		struct iovec iov;
		struct msghdr msg;
		ssize_t ret;

		iov.iov_base = buf;
		iov.iov_len = bufsize;
		memset(&msg, 0, sizeof(msg));
		msg.msg_name = &addr;
		msg.msg_namelen = sizeof(addr);
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;

		ret = recvmsg(GS_SOCK(s), &msg, 0);

		if (ret < 0)
		{
			return (errno == ECONNREFUSED) ? -2 : -1;
		}

		if (msg.msg_flags & MSG_TRUNC)
		{
			return -2; /* longer than the buffer: ignored like on Windows */
		}

		GS_FromSockaddr(&addr, from);
		return (int)ret;
	}
#endif
}

gssock_t
GS_TCPConnect(const gsaddr_t *to)
{
	struct sockaddr_in addr;
	gssock_t s = GS_NewSocket(SOCK_STREAM, IPPROTO_TCP);

	if (s == GS_NOSOCK)
	{
		return GS_NOSOCK;
	}

#ifdef SO_NOSIGPIPE
	{
		/* no MSG_NOSIGNAL on some BSDs: a send to a closed master must not
		   raise SIGPIPE */
		int one = 1;

		setsockopt(GS_SOCK(s), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
	}
#endif

	GS_ToSockaddr(to, &addr);

	if ((connect(GS_SOCK(s), (struct sockaddr *)&addr, sizeof(addr)) != 0) &&
		!GS_WouldBlock())
	{
		GS_Close(s);
		return GS_NOSOCK;
	}

	return s;
}

int
GS_TCPConnected(gssock_t s)
{
	fd_set wfds, efds;
	struct timeval tv = {0, 0};
	int err = 0;
	socklen_t_gs len = sizeof(err);

	if (s == GS_NOSOCK)
	{
		return -1;
	}

	FD_ZERO(&wfds);
	FD_ZERO(&efds);
	FD_SET(GS_SOCK(s), &wfds);
	FD_SET(GS_SOCK(s), &efds);

	if (select((int)GS_SOCK(s) + 1, NULL, &wfds, &efds, &tv) < 0)
	{
		return -1;
	}

	if (FD_ISSET(GS_SOCK(s), &efds))
	{
		return -1; /* Windows reports a failed connect here */
	}

	if (!FD_ISSET(GS_SOCK(s), &wfds))
	{
		return 0;
	}

	if (getsockopt(GS_SOCK(s), SOL_SOCKET, SO_ERROR, (char *)&err, &len) != 0)
	{
		return -1;
	}

	return err ? -1 : 1;
}

int
GS_TCPSend(gssock_t s, const void *data, int len)
{
	int ret;

#ifdef _WIN32
	ret = send(GS_SOCK(s), (const char *)data, len, 0);
#else
	ret = (int)send(GS_SOCK(s), data, len, MSG_NOSIGNAL);
#endif

	if (ret < 0)
	{
		return GS_WouldBlock() ? 0 : -1;
	}

	return ret;
}

int
GS_TCPRecv(gssock_t s, void *buf, int bufsize)
{
	int ret = (int)recv(GS_SOCK(s), (char *)buf, bufsize, 0);

	if (ret > 0)
	{
		return ret;
	}

	if (ret == 0)
	{
		return -1; /* closed by the peer */
	}

	return GS_WouldBlock() ? 0 : -1;
}
