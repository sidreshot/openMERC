/*
 * Copyright (C) 1997-2001 Id Software, Inc.
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
 * Connectionless server commands.
 *
 * OpenMerc: the out-of-band commands of the SoF 1.07f server (ping/ack
 * with argument, the SOF+ info reply, the status text, challenges keyed
 * by address and port, the protocol-33 connect checks with the LAN rule,
 * violence settings, reconnect limit and the game's two connect
 * exports, rcon with apostrophe translation).
 *
 * Implements: SPEC-200 (out-of-band commands, LAN test, connect, rcon),
 *             SPEC-001 ("Connect"), net-verification.md items 1, 5
 *
 * =======================================================================
 */

#include "header/server.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

/* ---------------------------------------------------------------------
 * the "LAN address" test (SPEC-200)
 * --------------------------------------------------------------------- */

static byte sv_localips[4][4];
static int sv_numlocalips = -1;

static void
SV_FindLocalAddresses(void)
{
	char host[256];
	struct addrinfo hints, *res = NULL, *p;

	sv_numlocalips = 0;

	if (gethostname(host, sizeof(host)) != 0)
	{
		return;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;

	if (getaddrinfo(host, NULL, &hints, &res) != 0)
	{
		return;
	}

	for (p = res; p && (sv_numlocalips < 4); p = p->ai_next)
	{
		const struct sockaddr_in *sin = (const struct sockaddr_in *)p->ai_addr;

		memcpy(sv_localips[sv_numlocalips], &sin->sin_addr, 4);
		sv_numlocalips++;
	}

	freeaddrinfo(res);
}

static qboolean
SV_SameClassfulNet(const byte *a, const byte *b)
{
	if (a[0] < 128)
	{
		return a[0] == b[0];
	}

	if (a[0] < 192)
	{
		return (a[0] == b[0]) && (a[1] == b[1]);
	}

	return (a[0] == b[0]) && (a[1] == b[1]) && (a[2] == b[2]);
}

qboolean
SV_IsLANAddress(netadr_t adr)
{
	int i;
	const byte *ip = adr.ip;

	/* the engine's own loopback, not 127.0.0.0/8 (net-verification.md 5) */
	if ((adr.type == NA_LOOPBACK) || (adr.type == NA_IPX) || (adr.type == NA_BROADCAST_IPX))
	{
		return true;
	}

	if (adr.type != NA_IP)
	{
		return false;
	}

	if (sv_numlocalips < 0)
	{
		SV_FindLocalAddresses();
	}

	for (i = 0; i < sv_numlocalips; i++)
	{
		const byte *l = sv_localips[i];

		if (SV_SameClassfulNet(ip, l))
		{
			return true;
		}

		if ((ip[0] == 172) && ((ip[1] & 0xF0) == 16) && (l[0] == 172) && ((l[1] & 0xF0) == 16))
		{
			return true;
		}

		if ((ip[0] == 192) && (ip[1] == 168) && (l[0] == 192) && (l[1] == 168))
		{
			return true;
		}
	}

	return false;
}

/* ---------------------------------------------------------------------
 * commands
 * --------------------------------------------------------------------- */

/*
 * Responds with all the info that qplug or qspy can see, split into
 * datagram-sized pieces
 */
static void
SVC_Status(void)
{
	const char *s = SV_StatusString();
	char piece[SV_OUTPUTBUF_LENGTH + 1];
	int len = (int)strlen(s), ofs = 0, n;

	do
	{
		n = len - ofs;

		if (n > SV_OUTPUTBUF_LENGTH)
		{
			n = SV_OUTPUTBUF_LENGTH;
		}

		memcpy(piece, s + ofs, n);
		piece[n] = 0;
		Netchan_OutOfBandPrint(NS_SERVER, net_from, "print\n%s", piece);
		ofs += n;
	}
	while (ofs < len);
}

static void
SVC_Ack(void)
{
	Com_Printf("Ping acknowledge from %s\n", NET_AdrToString(net_from));
}

/* SPEC-200 info reply */
static void
SVC_Info(void)
{
	int i, players = 0, spectators = 0;
	const char *spec;

	if (strcmp(Cmd_Argv(1), "33"))
	{
		return;
	}

	if ((maxclients->value <= 1) || !Cvar_VariableValue("deathmatch"))
	{
		return;
	}

	for (i = 0; i < svs.numclients; i++)
	{
		if (svs.clients[i].state >= cs_connected)
		{
			spec = Info_ValueForKey(svs.clients[i].userinfo, "spectator");

			if (spec[0] && strcmp(spec, "0"))
			{
				spectators++;
			}
			else
			{
				players++;
			}
		}
	}

	Netchan_OutOfBandPrint(NS_SERVER, net_from, "info\nSOF+%16s+%8s+%i/%i/%i+%s+%s+%i\n",
			hostname->string, sv.name, players, spectators, (int)maxclients->value,
			Cvar_VariableString("game"), SV_GameTypeName(),
			(int)Cvar_VariableValue("sv_violence"));
}

/* ping <token> -> ack <token> (with no argument "ack ") */
static void
SVC_Ping(void)
{
	Netchan_OutOfBandPrint(NS_SERVER, net_from, "ack %s", Cmd_Argv(1));
}

/*
 * Returns a challenge number that can be used in a subsequent connect.
 * Slots are keyed by address and port (net-verification.md item 1).
 */
static void
SVC_GetChallenge(void)
{
	int i;
	int oldest;
	int oldestTime;

	oldest = 0;
	oldestTime = 0x7fffffff;

	/* see if we already have a challenge for this address */
	for (i = 0; i < MAX_CHALLENGES; i++)
	{
		if (NET_CompareAdr(net_from, svs.challenges[i].adr))
		{
			break;
		}

		if (svs.challenges[i].time < oldestTime)
		{
			oldestTime = svs.challenges[i].time;
			oldest = i;
		}
	}

	if (i == MAX_CHALLENGES)
	{
		/* overwrite the oldest: two 15-bit random numbers */
		svs.challenges[oldest].challenge = (rand() & 0x7fff) | ((rand() & 0x7fff) << 16);
		svs.challenges[oldest].adr = net_from;
		svs.challenges[oldest].time = curtime;
		i = oldest;
	}

	/* send it back */
	Netchan_OutOfBandPrint(NS_SERVER, net_from, "challenge %i", svs.challenges[i].challenge);
}

static void
SV_Reject(netadr_t adr, const char *kind, const char *text)
{
	if (kind)
	{
		Netchan_OutOfBandPrint(NS_SERVER, adr, "%s", kind);
	}

	Netchan_OutOfBandPrint(NS_SERVER, adr, "print\n%s", text);
}

/*
 * A connection request (SPEC-200 "connect", checks in order)
 */
static void
SVC_DirectConnect(void)
{
	char userinfo[MAX_INFO_STRING];
	netadr_t adr;
	int i, c, s;
	client_t *cl, *newcl;
	edict_t *ent;
	int qport;
	int challenge;
	float cdvalue;
	qboolean lan;

	adr = net_from;

	Com_DPrintf("SVC_DirectConnect ()\n");

	/* 1. protocol */
	if ((int)strtol(Cmd_Argv(1), (char **)NULL, 10) != SV_PROTOCOL_VERSION)
	{
		Netchan_OutOfBandPrint(NS_SERVER, adr, "print\nServer is version 1.07f.\n");
		Com_DPrintf("    rejected connect from version %s\n", Cmd_Argv(1));
		return;
	}

	qport = (int)strtol(Cmd_Argv(2), (char **)NULL, 10);
	challenge = (int)strtol(Cmd_Argv(3), (char **)NULL, 10);
	cdvalue = (float)atof(Cmd_Argv(5));
	Q_strlcpy(userinfo, Cmd_Argv(6), 512);
	lan = SV_IsLANAddress(adr);

	/* 2. challenge, not checked for LAN clients */
	if (!lan)
	{
		for (i = 0; i < MAX_CHALLENGES; i++)
		{
			if (NET_CompareAdr(net_from, svs.challenges[i].adr))
			{
				if (challenge == svs.challenges[i].challenge)
				{
					break; /* good */
				}

				SV_Reject(adr, "rejected_error -1\n", "Bad challenge.\n");
				return;
			}
		}

		if (i == MAX_CHALLENGES)
		{
			SV_Reject(adr, "rejected_error -1\n", "No challenge for address.\n");
			return;
		}
	}

	/* 3. violence settings */
	c = (int)strtol(Info_ValueForKey(userinfo, "cl_violence"), (char **)NULL, 10);
	s = (int)Cvar_VariableValue("sv_violence");

	if ((s & c) != c)
	{
		SV_Reject(adr, "rejected_violence\n", "Server violence settings are incompatible.\n");
		return;
	}

	/* 4. the address, so the game can filter based on it */
	Info_SetValueForKey(userinfo, "ip", NET_AdrToString(net_from));

	/* 5. attract loops are only for local clients */
	if (sv.attractloop && !lan)
	{
		Netchan_OutOfBandPrint(NS_SERVER, adr, "print\nConnection refused.\n");
		return;
	}

	/* 6. a slot of the same address and qport/port is reused, unless it
	   connected less than sv_reconnect_limit seconds ago */
	newcl = NULL;

	for (i = 0, cl = svs.clients; i < svs.numclients; i++, cl++)
	{
		/* a zombie slot (the client sent `disconnect` before reconnecting, as the
		   OpenMerc client does on `reconnect`) still belongs to its address
		   (Quake II skips only free slots); skipping it left the local client of
		   a single-player level change "Server is full" (I1b, 2026-10-25) */
		if (cl->state == cs_free)
		{
			continue;
		}

		if (NET_CompareBaseAdr(adr, cl->netchan.remote_address) &&
			((cl->netchan.qport == qport) ||
			 (adr.port == cl->netchan.remote_address.port)))
		{
			if ((svs.realtime - cl->lastconnect) < (int)(sv_reconnect_limit->value * 1000))
			{
				Com_DPrintf("%s:reconnect rejected : too soon\n", NET_AdrToString(adr));
				return;
			}

			Com_Printf("%s:reconnect\n", NET_AdrToString(adr));

			if (cl->entered)
			{
				if (ge->ClientDropped)
				{
					ge->ClientDropped(CL_EDICT(cl));
				}

				ge->ClientDisconnect(CL_EDICT(cl));
			}

			newcl = cl;
			break;
		}
	}

	/* 7. find a client slot */
	if (!newcl)
	{
		for (i = 0, cl = svs.clients; i < svs.numclients; i++, cl++)
		{
			if (cl->state == cs_free)
			{
				newcl = cl;
				break;
			}
		}
	}

	if (!newcl)
	{
		SV_Reject(adr, "rejected_full\n", "Server is full.\n");
		Com_DPrintf("Rejected a connection.\n");
		return;
	}

	ent = CL_EDICT(newcl);

	/* 8. the game decides: pre-connect first, before the slot is committed */
	if (ge->ClientPreConnect && !ge->ClientPreConnect(ent, userinfo))
	{
		goto refused;
	}

	/* commit the slot: the game's connect messages (its player-info
	   configstring) go to this client's reliable stream first
	   (net-verification.md item 8) */
	memset(newcl, 0, sizeof(*newcl));
	sv_client = newcl;
	newcl->challenge = challenge;
	newcl->cdvalue = cdvalue / 1.35f * 4;
	newcl->messagelevel = 0;
	Netchan_Setup(NS_SERVER, &newcl->netchan, adr, qport);
	newcl->state = cs_connected;
	SZ_Init(&newcl->datagram, newcl->datagram_buf, SV_DATAGRAM_LIMIT);
	newcl->datagram.allowoverflow = true;
	newcl->lastmessage = svs.realtime; /* don't timeout */
	newcl->lastconnect = svs.realtime;
	newcl->lastframe = -1;

	if (!ge->ClientConnect(ent, userinfo))
	{
		memset(newcl, 0, sizeof(*newcl));
		newcl->state = cs_free;
		goto refused;
	}

	/* parse some info from the info strings */
	Q_strlcpy(newcl->userinfo, userinfo, sizeof(newcl->userinfo));
	SV_UserinfoChanged(newcl, false);

	/* 9. the connect packet */
	Netchan_OutOfBandPrint(NS_SERVER, adr, "client_connect");
	return;

refused:

	if (*Info_ValueForKey(userinfo, "rejmsg"))
	{
		Netchan_OutOfBandPrint(NS_SERVER, adr, "%s", Info_ValueForKey(userinfo, "rejmsg"));
		Netchan_OutOfBandPrint(NS_SERVER, adr, "print\n%s\nConnection refused.\n",
				Info_ValueForKey(userinfo, "rejmsg"));
	}
	else
	{
		Netchan_OutOfBandPrint(NS_SERVER, adr, "print\nConnection refused.\n");
	}

	Com_DPrintf("Game rejected a connection.\n");
}

static int
Rcon_Validate(void)
{
	if (!strlen(rcon_password->string))
	{
		return 0;
	}

	if (strcmp(Cmd_Argv(1), rcon_password->string))
	{
		return 0;
	}

	return 1;
}

/*
 * A client issued an rcon command: the words from 2 on, each followed by a
 * space, apostrophes become double quotes; the output goes back in print
 * packets.
 */
static void
SVC_RemoteCommand(void)
{
	int i;
	char remaining[1024], *p;

	i = Rcon_Validate();

	if (i == 0)
	{
		Com_Printf("Bad rcon from %s:\n%s\n", NET_AdrToString(net_from),
				net_message.data + 4);
	}
	else
	{
		Com_Printf("Rcon from %s:\n%s\n", NET_AdrToString(net_from),
				net_message.data + 4);
	}

	Com_BeginRedirect(RD_PACKET, sv_outputbuf, SV_OUTPUTBUF_LENGTH, SV_FlushRedirect);

	if (!Rcon_Validate())
	{
		Com_Printf("Bad rcon_password.\n");
	}
	else
	{
		remaining[0] = 0;

		for (i = 2; i < Cmd_Argc(); i++)
		{
			Q_strlcat(remaining, Cmd_Argv(i), sizeof(remaining));
			Q_strlcat(remaining, " ", sizeof(remaining));
		}

		for (p = remaining; *p; p++)
		{
			if (*p == '\'')
			{
				*p = '"';
			}
		}

		Cmd_ExecuteString(remaining);
	}

	Com_EndRedirect();
}

/*
 * A connectionless packet has four leading 0xff
 * characters to distinguish it from a game channel.
 * Clients that are in the game can still send
 * connectionless packets.
 */
void
SV_ConnectionlessPacket(void)
{
	char *s;
	char *c;

	MSG_BeginReading(&net_message);
	MSG_ReadLong(&net_message); /* skip the -1 marker */

	s = MSG_ReadStringLine(&net_message);

	Cmd_TokenizeString(s, false);

	c = Cmd_Argv(0);
	Com_DPrintf("Packet %s : %s\n", NET_AdrToString(net_from), c);

	if (!strcmp(c, "ping"))
	{
		SVC_Ping();
	}
	else if (!strcmp(c, "ack"))
	{
		SVC_Ack();
	}
	else if (!strcmp(c, "status"))
	{
		SVC_Status();
	}
	else if (!strcmp(c, "info"))
	{
		SVC_Info();
	}
	else if (!strcmp(c, "getchallenge"))
	{
		SVC_GetChallenge();
	}
	else if (!strcmp(c, "connect"))
	{
		SVC_DirectConnect();
	}
	else if (!strcmp(c, "rcon"))
	{
		SVC_RemoteCommand();
	}
	else
	{
		Com_Printf("Bad connectionless packet from %s:\n%s\n",
				NET_AdrToString(net_from), s);
	}
}
