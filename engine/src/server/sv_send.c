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
 * Message sending and multiplexing.
 *
 * OpenMerc: SoF 1.07f multicast (six destinations plus the excluded
 * client), svc_sound with the 9-bit index and the per-listener position
 * rule, prints with message levels, the datagram order of SPEC-201
 * (reliable part, culled events, frame, Ghoul unreliable, queued
 * messages) and the Ghoul reliable stream in game.
 *
 * Implements: SPEC-001 (#22, #23, #28-#30), SPEC-201 (delivery rules,
 *             s10, s11, s26/27), SPEC-200 (datagram limit), SPEC-013 s4
 *
 * =======================================================================
 */

#include "header/server.h"

char sv_outputbuf[SV_OUTPUTBUF_LENGTH];

/* sound flags (SPEC-201 s10) */
#define SSND_VOLUME 0x01
#define SSND_ATTENUATION 0x02
#define SSND_POS 0x04
#define SSND_ENT 0x08
#define SSND_OFFSET 0x10
#define SSND_INDEX256 0x20

#define SOF_CHAN_NO_PHS_ADD 8
#define SOF_CHAN_RELIABLE 16

/* Bots are game-side clients without a connection: the engine slot of
   their entity is not connected, so nothing is sent to them. */
qboolean
SV_ClientIsBot(const client_t *cl)
{
	return !cl || (cl->state < cs_connected);
}

void
SV_FlushRedirect(int sv_redirected, char *outputbuf)
{
	if (sv_redirected == RD_PACKET)
	{
		Netchan_OutOfBandPrint(NS_SERVER, net_from, "print\n%s", outputbuf);
	}
	else if (sv_redirected == RD_CLIENT)
	{
		MSG_WriteByte(&sv_client->netchan.message, sofsvc_print);
		MSG_WriteByte(&sv_client->netchan.message, PRINT_HIGH);
		MSG_WriteString(&sv_client->netchan.message, outputbuf);
	}
}

/*
 * Sends text across to be displayed if the level passes
 */
void
SV_ClientPrintf(client_t *cl, int level, const char *fmt, ...)
{
	va_list argptr;
	char string[1024];

	va_start(argptr, fmt);
	vsnprintf(string, sizeof(string), fmt, argptr);
	va_end(argptr);

	MSG_WriteByte(&cl->netchan.message, sofsvc_print);
	MSG_WriteByte(&cl->netchan.message, level);
	MSG_WriteString(&cl->netchan.message, string);
}

/*
 * Sends text to all active clients (SPEC-001 #12: clients in the game
 * whose message level allows it; echoed on a dedicated server)
 */
void
SV_BroadcastPrintf(int level, const char *fmt, ...)
{
	va_list argptr;
	char string[2048];
	client_t *cl;
	int i;

	va_start(argptr, fmt);
	vsnprintf(string, sizeof(string), fmt, argptr);
	va_end(argptr);

	/* echo to console */
	if (dedicated->value)
	{
		char copy[1024];
		int j;

		/* mask off high bits */
		for (j = 0; j < 1023 && string[j]; j++)
		{
			copy[j] = string[j] & 127;
		}

		copy[j] = 0;
		Com_Printf("%s", copy);
	}

	for (i = 0, cl = svs.clients; i < svs.numclients; i++, cl++)
	{
		if (level < cl->messagelevel)
		{
			continue;
		}

		if (cl->state != cs_spawned)
		{
			continue;
		}

		MSG_WriteByte(&cl->netchan.message, sofsvc_print);
		MSG_WriteByte(&cl->netchan.message, level);
		MSG_WriteString(&cl->netchan.message, string);
	}
}

/*
 * Sends text to all active clients
 */
void
SV_BroadcastCommand(const char *fmt, ...)
{
	va_list argptr;
	char string[1024];

	if (!sv.state)
	{
		return;
	}

	va_start(argptr, fmt);
	vsnprintf(string, sizeof(string), fmt, argptr);
	va_end(argptr);

	MSG_WriteByte(&sv.multicast, sofsvc_stufftext);
	MSG_WriteString(&sv.multicast, string);
	SV_Multicast(NULL, SOF_MULTICAST_ALL_R, 0);
}

/* the point a client hears from: its player origin, or the remote camera */
static void
SV_ClientListenOrigin(client_t *client, vec3_t org)
{
	edict_t *e = CL_EDICT(client);

	if (e->client && (e->client->ps.remote_type > 0))
	{
		org[0] = e->client->ps.remote_origin[0] * 0.125f;
		org[1] = e->client->ps.remote_origin[1] * 0.125f;
		org[2] = e->client->ps.remote_origin[2] * 0.125f;
		return;
	}

	VectorCopy(e->s.origin, org);
}

static qboolean
SV_ClientInSet(client_t *client, const byte *mask, int area1)
{
	vec3_t org;
	int leafnum, cluster, area2;

	SV_ClientListenOrigin(client, org);
	leafnum = CM_PointLeafnum(org);
	cluster = CM_LeafCluster(leafnum);
	area2 = CM_LeafArea(leafnum);

	if ((cluster < 0) || !(mask[cluster >> 3] & (1 << (cluster & 7))))
	{
		return false;
	}

	return CM_AreasConnected(area1, area2);
}

/*
 * Sends the contents of sv.multicast to a subset of the clients,
 * then clears sv.multicast (SPEC-001 #28/#29, SPEC-201 delivery rules).
 */
void
SV_Multicast(const vec3_t origin, int to, int excludeent)
{
	int leafnum, cluster, area1 = 0, j;
	qboolean reliable = false;
	client_t *client;
	byte *mask = NULL;

	if ((to < SOF_MULTICAST_ALL) || (to > SOF_MULTICAST_PVS_R))
	{
		Com_Error(ERR_FATAL, "SV_Multicast: bad to:%i", to);
	}

	/* if doing a serverrecord, store everything */
	if (svs.demofile)
	{
		SZ_Write(&svs.demo_multicast, sv.multicast.data, sv.multicast.cursize);
	}

	switch (to)
	{
		case SOF_MULTICAST_ALL_R:
			reliable = true; /* intentional fallthrough */
		case SOF_MULTICAST_ALL:
			mask = NULL;
			break;

		case SOF_MULTICAST_PHS_R:
			reliable = true; /* intentional fallthrough */
		case SOF_MULTICAST_PHS:
			leafnum = CM_PointLeafnum(origin);
			cluster = CM_LeafCluster(leafnum);
			area1 = CM_LeafArea(leafnum);
			mask = CM_ClusterPHS(cluster);
			break;

		case SOF_MULTICAST_PVS_R:
			reliable = true; /* intentional fallthrough */
		case SOF_MULTICAST_PVS:
			leafnum = CM_PointLeafnum(origin);
			cluster = CM_LeafCluster(leafnum);
			area1 = CM_LeafArea(leafnum);
			mask = CM_ClusterPVS(cluster);
			break;
	}

	/* send the data to all relevent clients */
	for (j = 0, client = svs.clients; j < svs.numclients; j++, client++)
	{
		if ((client->state == cs_free) || (client->state == cs_zombie))
		{
			continue;
		}

		if ((client->state != cs_spawned) && !reliable)
		{
			continue;
		}

		if (excludeent && (j + 1 == excludeent))
		{
			continue;
		}

		if (mask && !SV_ClientInSet(client, mask, area1))
		{
			continue;
		}

		SZ_Write(reliable ? &client->netchan.message : &client->datagram,
				sv.multicast.data, sv.multicast.cursize);
	}

	SZ_Clear(&sv.multicast);
}

/*
 * Sends the contents of the multicast buffer to a single client
 */
void
SV_Unicast(const edict_t *ent, qboolean reliable)
{
	int p;
	client_t *client;

	if (!ent)
	{
		SZ_Clear(&sv.multicast);
		return;
	}

	p = NUM_FOR_EDICT(ent);

	if ((p < 1) || (p > svs.numclients))
	{
		SZ_Clear(&sv.multicast);
		return;
	}

	client = svs.clients + (p - 1);

	if (!SV_ClientIsBot(client))
	{
		SZ_Write(reliable ? &client->netchan.message : &client->datagram,
				sv.multicast.data, sv.multicast.cursize);
	}

	SZ_Clear(&sv.multicast);
}

/* one svc_sound for one listener (SPEC-201 s10) */
static void
SV_WriteSound(sizebuf_t *buf, int flags, int soundindex, float volume, float attenuation,
		float timeofs, int ent, int channel, const vec3_t origin)
{
	MSG_WriteByte(buf, sofsvc_sound);
	MSG_WriteByte(buf, flags);
	MSG_WriteByte(buf, soundindex & 255);

	if (flags & SSND_VOLUME)
	{
		MSG_WriteByte(buf, (int)(volume * 255));
	}

	if (flags & SSND_ATTENUATION)
	{
		MSG_WriteByte(buf, (int)(attenuation * 64));
	}

	if (flags & SSND_OFFSET)
	{
		MSG_WriteByte(buf, (int)(timeofs * 1000));
	}

	if (flags & SSND_ENT)
	{
		MSG_WriteShort(buf, (ent << 3) | (channel & 7));
	}

	if (flags & SSND_POS)
	{
		SV_WritePos(buf, origin);
	}
}

/*
 * Each entity can have eight independant sound sources, like voice,
 * weapon, feet, etc. Channel bit 8: everyone (no PHS culling); bit 16:
 * reliable; attenuation 0: everyone. `local': only the entity's own
 * client, without position.
 */
void
SV_StartSound(const vec3_t origin, const edict_t *entity, int channel, int soundindex,
		float volume, float attenuation, float timeofs, int local)
{
	int flags, basicflags, ent, j, leafnum, cluster, area1;
	vec3_t origin_v;
	qboolean use_phs, reliable, forcepos;
	client_t *client;
	byte *phs = NULL, *pvs = NULL;

	ent = entity ? NUM_FOR_EDICT(entity) : 0;

	if (ent > 1024)
	{
		return;
	}

	basicflags = SSND_ENT;

	if (volume != 1.0f)
	{
		basicflags |= SSND_VOLUME;
	}

	if (attenuation != 1.0f)
	{
		basicflags |= SSND_ATTENUATION;
	}

	if (timeofs)
	{
		basicflags |= SSND_OFFSET;
	}

	if (soundindex >= 256)
	{
		basicflags |= SSND_INDEX256;
	}

	/* the client does not know where unsent or brush entities are */
	forcepos = (origin != NULL) || !entity ||
		(entity->svflags & SOF_SVF_NOCLIENT) ||
		(entity->solid == SOF_SOLID_BSP) || (entity->solid == SOF_SOLID_BSP2);

	/* use the entity origin unless it is a bmodel or explicitly specified */
	if (!origin)
	{
		origin = origin_v;

		if (entity && ((entity->solid == SOF_SOLID_BSP) || (entity->solid == SOF_SOLID_BSP2)))
		{
			for (j = 0; j < 3; j++)
			{
				origin_v[j] = entity->s.origin[j] + 0.5f *
					(entity->mins[j] + entity->maxs[j]);
			}
		}
		else if (entity)
		{
			VectorCopy(entity->s.origin, origin_v);
		}
		else
		{
			VectorClear(origin_v);
		}
	}

	reliable = (channel & SOF_CHAN_RELIABLE) ? true : false;

	if (local)
	{
		/* only the player that owns the entity hears it */
		if ((ent >= 1) && (ent <= svs.numclients))
		{
			client = svs.clients + (ent - 1);

			if (!SV_ClientIsBot(client) && (client->state == cs_spawned))
			{
				SV_WriteSound(reliable ? &client->netchan.message : &client->datagram,
						basicflags, soundindex, volume, attenuation, timeofs, ent,
						channel, origin);
			}
		}

		return;
	}

	use_phs = (channel & SOF_CHAN_NO_PHS_ADD) ? false : true;

	/* if the sound doesn't attenuate, send it to everyone */
	if (attenuation == 0.0f)
	{
		use_phs = false;
	}

	leafnum = CM_PointLeafnum(origin);
	cluster = CM_LeafCluster(leafnum);
	area1 = CM_LeafArea(leafnum);

	if (use_phs)
	{
		phs = CM_ClusterPHS(cluster);
		pvs = CM_ClusterPVS(cluster);
	}

	flags = basicflags | (forcepos ? SSND_POS : 0);

	for (j = 0, client = svs.clients; j < svs.numclients; j++, client++)
	{
		if ((client->state == cs_free) || (client->state == cs_zombie))
		{
			continue;
		}

		if ((client->state != cs_spawned) && !reliable)
		{
			continue;
		}

		if (phs)
		{
			if (!SV_ClientInSet(client, phs, area1))
			{
				continue;
			}

			/* listeners that cannot see the origin get the position; the
			   flag then stays set for the later listeners (original quirk) */
			if (!SV_ClientInSet(client, pvs, area1))
			{
				flags |= SSND_POS;
			}
		}

		SV_WriteSound(reliable ? &client->netchan.message : &client->datagram,
				phs ? flags : (basicflags | (forcepos ? SSND_POS : 0)),
				soundindex, volume, attenuation, timeofs, ent, channel, origin);
	}
}

/* in game, the Ghoul reliable stream rides along whenever more than 16
   bytes of the half-datagram budget are free (SPEC-201 s26) */
static void
SV_AddGhoulReliable(client_t *client)
{
	int clientnum = (int)(client - svs.clients);
	int free, len, done;
	byte buf[SV_MAX_DATAGRAM];

	if (!client->ghoulknown || !SV_Ghoul_ReliablePending(clientnum))
	{
		return;
	}

	free = SV_SIGNON_BATCH - client->netchan.message.cursize;

	if (free <= 16)
	{
		return;
	}

	len = SV_Ghoul_WriteReliable(clientnum, buf, free - 3, &done);

	if (len > 0)
	{
		MSG_WriteByte(&client->netchan.message, sofsvc_ghoulreliable);
		MSG_WriteShort(&client->netchan.message, len);
		SZ_Write(&client->netchan.message, buf, len);
	}
}

static qboolean
SV_SendClientDatagram(client_t *client)
{
	byte msg_buf[SV_MAX_DATAGRAM];
	byte ghoul_buf[SV_MAX_DATAGRAM];
	sizebuf_t msg;
	int clientnum = (int)(client - svs.clients);
	int budget, len;

	SV_BuildClientFrame(client);
	SV_AddGhoulReliable(client);

	SZ_Init(&msg, msg_buf, SV_DATAGRAM_LIMIT - 16);
	msg.allowoverflow = true;

	/* culled events, then the frame with player state and entities */
	SV_WriteFrameToClient(client, &msg);

	/* at most one Ghoul unreliable message, right after the frame */
	budget = (int)sv_max_ghoul_mp->value;

	if (budget < 100)
	{
		budget = 100;
	}

	if (maxclients->value <= 1)
	{
		budget = msg.maxsize; /* not limited outside network games */
	}

	if (budget > msg.maxsize - msg.cursize - client->netchan.message.cursize - 3)
	{
		budget = msg.maxsize - msg.cursize - client->netchan.message.cursize - 3;
	}

	if (budget > 0)
	{
		len = SV_Ghoul_WriteUnreliable(clientnum, sv.framenum, sv.gametime, ghoul_buf, budget);

		if (len > 0)
		{
			MSG_WriteByte(&msg, sofsvc_ghoulunreliable);
			MSG_WriteShort(&msg, len);
			SZ_Write(&msg, ghoul_buf, len);
		}
	}

	SV_Ghoul_CommitFrame(clientnum, sv.framenum);

	/* copy the accumulated multicast datagram for this client out to the
	   message, after the frame so that entity references are current */
	if (client->datagram.overflowed)
	{
		Com_DPrintf("WARNING: datagram overflowed for %s\n", client->name);
	}
	else
	{
		SZ_Write(&msg, client->datagram.data, client->datagram.cursize);
	}

	SZ_Clear(&client->datagram);

	if (msg.overflowed)
	{
		/* must have room left for the packet header */
		Com_DPrintf("WARNING: msg overflowed for %s\n", client->name);
		SZ_Clear(&msg);
	}

	/* send the datagram */
	Netchan_Transmit(&client->netchan, msg.cursize, msg.data);

	/* record the size for rate estimation */
	client->message_size[sv.framenum % RATE_MESSAGES] = msg.cursize;

	return true;
}

/*
 * Returns true if the client is over its current
 * bandwidth estimation and should not be sent another packet
 */
static qboolean
SV_RateDrop(client_t *c)
{
	int total;
	int i;

	/* never drop over the loopback */
	if (c->netchan.remote_address.type == NA_LOOPBACK)
	{
		return false;
	}

	total = 0;

	for (i = 0; i < RATE_MESSAGES; i++)
	{
		total += c->message_size[i];
	}

	if (total > c->rate)
	{
		c->surpressCount++;
		c->message_size[sv.framenum % RATE_MESSAGES] = 0;
		return true;
	}

	return false;
}

/* if the reliable message overflowed, drop the client ("You overflowed") */
static void
SV_SendDisconnect(client_t *c)
{
	SZ_Clear(&c->netchan.message);
	SZ_Clear(&c->datagram);

	SV_BroadcastPrintf(PRINT_HIGH, "%s overflowed\n", c->name);
	SV_DropClientWithReason(c, SV_STR_OVERFLOWED);

	Netchan_Transmit(&c->netchan, 0, NULL);
}

void
SV_SendClientMessages(void)
{
	int i;
	client_t *c;

	/* send a message to each spawned client */
	for (i = 0, c = svs.clients; i < svs.numclients; i++, c++)
	{
		if (c->state == cs_free)
		{
			continue;
		}

		if (c->netchan.message.overflowed)
		{
			SV_SendDisconnect(c);
			continue;
		}

		if (c->state == cs_spawned)
		{
			/* don't overrun bandwidth */
			if (SV_RateDrop(c))
			{
				continue;
			}

			SV_SendClientDatagram(c);
		}

		/* messages to non-spawned clients are sent by SendPrepClientMessages */
	}
}

void
SV_SendPrepClientMessages(void)
{
	client_t *c;
	int i;

	/* send a message to each inactive client if needed; keep-alive once a
	   second (net-verification.md item 11) */
	for (i = 0, c = svs.clients; i < svs.numclients; i++, c++)
	{
		if ((c->state == cs_free) || (c->state == cs_spawned))
		{
			continue;
		}

		if (c->netchan.message.overflowed)
		{
			SV_SendDisconnect(c);
			continue;
		}

		if (c->netchan.message.cursize || (curtime - c->netchan.last_sent > 1000))
		{
			Netchan_Transmit(&c->netchan, 0, NULL);
		}
	}
}
