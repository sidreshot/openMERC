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
 * Server main loop.
 *
 * OpenMerc: the SoF 1.07f server frame (game counter and game time,
 * entity events cleared after every frame, player-module context), drops
 * with string-package reasons, the SoF status text, the GameSpy
 * heartbeats (sv_gamespy.c) instead of the Quake II master protocol.
 *
 * Implements: SPEC-001 ("How the engine drives the exports"), SPEC-200
 *             (status text, timeouts), SPEC-201 s8 (disconnect, final
 *             messages), SPEC-202 s3.1/s6.4, SPEC-203 (userinfo keys),
 *             SPEC-204
 *
 * =======================================================================
 */

#include "header/server.h"
#include "../common/header/gamespy.h"

client_t *sv_client; /* current client */

cvar_t *sv_paused;
cvar_t *sv_timedemo;
cvar_t *sv_enforcetime;
cvar_t *timeout; /* seconds without any message */
cvar_t *zombietime; /* seconds to sink messages after disconnect */
cvar_t *rcon_password; /* password for remote server commands */
cvar_t *allow_download;
cvar_t *allow_download_maps;
cvar_t *allow_download_sounds;
cvar_t *allow_download_stringpackage;
cvar_t *allow_download_players; /* read by client/cl_download.c */
cvar_t *allow_download_models; /* read by client/cl_download.c */
cvar_t *sv_noreload; /* don't reload level state when reentering */
cvar_t *maxclients; /* rename sv_maxclients */
cvar_t *sv_showclamp;
cvar_t *hostname;
cvar_t *public_server; /* should heartbeats be sent */
cvar_t *sv_reconnect_limit;
cvar_t *sv_max_ghoul_mp;
cvar_t *sv_ghoul_stub;
cvar_t *sv_entfile; /* referenced by common code */
static cvar_t *sv_jumpcinematic;

void SV_ConnectionlessPacket(void);

/*
 * Called when the player is totally leaving the server, either willingly
 * or unwillingly.  This is NOT called if the entire server is quiting
 * or crashing.
 */
void
SV_DropClientWithReason(client_t *drop, int stringid)
{
	edict_t *ent = CL_EDICT(drop);

	/* add the disconnect */
	MSG_WriteByte(&drop->netchan.message, sofsvc_disconnect);
	MSG_WriteShort(&drop->netchan.message, stringid);

	/* SPEC-001: export 0x3C for every drop, ClientDisconnect only for
	   clients that had entered the game */
	if (ge && ge->ClientDropped)
	{
		ge->ClientDropped(ent);
	}

	if (drop->entered && ge)
	{
		ge->ClientDisconnect(ent);
	}

	drop->entered = false;

	if (drop->ghoulknown)
	{
		SV_Ghoul_ClientDropped((int)(drop - svs.clients));
		drop->ghoulknown = false;
	}

	if (drop->download)
	{
		FS_FreeFile(drop->download);
		drop->download = NULL;
	}

	drop->state = cs_zombie; /* become free in a few seconds */
	drop->name[0] = 0;
}

void
SV_DropClient(client_t *drop)
{
	SV_DropClientWithReason(drop, SV_STR_DISCONNECTED);
}

/* the score the game keeps in stats[10] (SPEC-780 s3) */
int
SV_ClientScore(client_t *cl)
{
	edict_t *ent;

	if (!ge || !cl)
	{
		return 0;
	}

	ent = CL_EDICT(cl);
	return ent->client ? ent->client->ps.stats[10] : 0;
}

/* the game mode's name, or the deathmatch value without a game module */
const char *
SV_GameTypeName(void)
{
	static char num[16];

	if (ge && ge->GetGameType)
	{
		return ge->GetGameType();
	}

	Com_sprintf(num, sizeof(num), "%i", (int)Cvar_VariableValue("deathmatch"));
	return num;
}

/*
 * Builds the string that is sent as status replies (SPEC-200: serverinfo,
 * a backslash and a newline, then one line per client)
 */
char *
SV_StatusString(void)
{
	char player[1024];
	static char status[MAX_MSGLEN - 16];
	int i;
	client_t *cl;
	int statusLength;
	int playerLength;

	Q_strlcpy(status, Cvar_Serverinfo(), sizeof(status));
	Q_strlcat(status, "\\\n", sizeof(status));
	statusLength = (int)strlen(status);

	for (i = 0; i < svs.numclients; i++)
	{
		cl = &svs.clients[i];

		if ((cl->state == cs_connected) || (cl->state == cs_spawned))
		{
			Com_sprintf(player, sizeof(player), "%i %i \"%s\"\n",
					SV_ClientScore(cl), cl->ping, cl->name);
			playerLength = (int)strlen(player);

			if (statusLength + playerLength >= MAX_MSGLEN)
			{
				break; /* can't hold any more */
			}

			strcpy(status + statusLength, player);
			statusLength += playerLength;
		}
	}

	return status;
}

/*
 * Updates the cl->ping variables
 */
static void
SV_CalcPings(void)
{
	int i, j;
	client_t *cl;
	int total, count;

	for (i = 0; i < svs.numclients; i++)
	{
		cl = &svs.clients[i];

		if (cl->state != cs_spawned)
		{
			continue;
		}

		total = 0;
		count = 0;

		for (j = 0; j < LATENCY_COUNTS; j++)
		{
			if (cl->frame_latency[j] > 0)
			{
				count++;
				total += cl->frame_latency[j];
			}
		}

		cl->ping = (!count) ? 0 : (total / count);

		/* let the game dll know about the ping */
		if (CL_EDICT(cl)->client)
		{
			CL_EDICT(cl)->client->ping = cl->ping;
		}
	}
}

/*
 * Every few frames, gives all clients an allotment of milliseconds
 * for their command moves. If they exceed it, assume cheating.
 */
static void
SV_GiveMsec(void)
{
	int i;
	client_t *cl;

	if (sv.framenum & 15)
	{
		return;
	}

	for (i = 0; i < svs.numclients; i++)
	{
		cl = &svs.clients[i];

		if (cl->state == cs_free)
		{
			continue;
		}

		cl->commandMsec = 1800; /* 1600 + some slop */
	}
}

static void
SV_ReadPackets(void)
{
	int i;
	client_t *cl;
	int qport;

	while (NET_GetPacket(NS_SERVER, &net_from, &net_message))
	{
		/* check for connectionless packet (0xffffffff) first */
		if (*(int *)net_message.data == -1)
		{
			SV_ConnectionlessPacket();
			continue;
		}

		if (net_message.cursize < 10)
		{
			continue;
		}

		/* read the qport out of the message so we can fix up
		   stupid address translating routers */
		MSG_BeginReading(&net_message);
		MSG_ReadLong(&net_message); /* sequence number */
		MSG_ReadLong(&net_message); /* sequence number */
		qport = MSG_ReadShort(&net_message) & 0xffff;

		/* check for packets from connected clients */
		for (i = 0, cl = svs.clients; i < svs.numclients; i++, cl++)
		{
			if (cl->state == cs_free)
			{
				continue;
			}

			if (!NET_CompareBaseAdr(net_from, cl->netchan.remote_address))
			{
				continue;
			}

			if (cl->netchan.qport != qport)
			{
				continue;
			}

			if (cl->netchan.remote_address.port != net_from.port)
			{
				Com_Printf("SV_ReadPackets: fixing up a translated port\n");
				cl->netchan.remote_address.port = net_from.port;
			}

			if (Netchan_Process(&cl->netchan, &net_message))
			{
				/* this is a valid, sequenced packet, so process it */
				if (cl->state != cs_zombie)
				{
					cl->lastmessage = svs.realtime; /* don't timeout */

					if (!(sv.demofile && (sv.state == ss_demo)))
					{
						SV_ExecuteClientMessage(cl);
					}
				}
			}

			break;
		}
	}
}

/*
 * If a packet has not been received from a client for timeout->value
 * seconds, drop the conneciton.
 */
static void
SV_CheckTimeouts(void)
{
	int i;
	client_t *cl;
	int droppoint;
	int zombiepoint;

	droppoint = svs.realtime - 1000 * timeout->value;
	zombiepoint = svs.realtime - 1000 * zombietime->value;

	for (i = 0, cl = svs.clients; i < svs.numclients; i++, cl++)
	{
		/* message times may be wrong across a changelevel */
		if (cl->lastmessage > svs.realtime)
		{
			cl->lastmessage = svs.realtime;
		}

		if ((cl->state == cs_zombie) &&
			(cl->lastmessage < zombiepoint))
		{
			cl->state = cs_free; /* can now be reused */
			continue;
		}

		if (((cl->state == cs_connected) || (cl->state == cs_spawned)) &&
			(cl->lastmessage < droppoint) && !SV_ClientIsBot(cl))
		{
			SV_BroadcastPrintf(PRINT_HIGH, "%s timed out\n", cl->name);
			SV_DropClientWithReason(cl, SV_STR_TIMEDOUT);
			cl->state = cs_free; /* don't bother with zombie state */
		}
	}
}

static void
SV_RunGameFrame(void)
{
	int batch;

	/* we always need to bump framenum, even if we
	   don't run the world, otherwise the delta
	   compression can get confused when a client
	   has the "current" frame */
	sv.framenum++;
	sv.time = sv.framenum * 100;

	/* don't run if paused */
	if (sv_paused->value && (maxclients->value <= 1))
	{
		SV_HostDebug(SV_DBG_THINK, "frame %i: paused, game not run\n", sv.framenum);
		return;
	}

	SV_Watchdog_Mark("game RunFrame");
	SV_GameRunFrame(sv.framenum);
	SV_Watchdog_Mark("after game RunFrame");
	SV_ImportTrace_ScanEdicts();

	/* SPEC-001 cinematic fast-forward: frames back to back while
	   sv_jumpcinematic is set and a cinematic is running */
	if (sv_jumpcinematic->value && SV_GameCinematicState())
	{
		Cvar_Set("s_disable", "1");
		ge->SetCinematicState(2);

		for (batch = 0; batch < 50 && SV_GameCinematicState(); batch++)
		{
			SV_GameRunFrame(sv.gameframe + 1);
		}

		if (!SV_GameCinematicState())
		{
			Cvar_Set("sv_jumpcinematic", "0");
			Cvar_Set("s_disable", "0");
		}
	}

	/* never get more than one tic behind */
	if (sv.time < svs.realtime)
	{
		if (sv_showclamp->value)
		{
			Com_Printf("sv highclamp\n");
		}

		svs.realtime = sv.time;
	}
}

void
SV_Frame(int usec)
{
	/* answer GameSpy queries; every process does, server running or not */
	SV_GameSpy_Frame();

	/* if server is not active, do nothing */
	if (!svs.initialized)
	{
		return;
	}

	SV_Watchdog_Beat("SV_Frame");
	/* ENG2: keep the sub-millisecond remainder; SV_Frame now runs every host frame
	   with a local client (SPEC-AMEND-010 M1/M10) */
	{
		static int usec_rem;

		usec += usec_rem;
		svs.realtime += usec / 1000;
		usec_rem = usec % 1000;
	}

	/* I1d: SPEC-003 "Context flag" - the server writes 0 at the start of every server frame; the client
	   (cl_player.c) writes -1 at the start of every client frame, so the clients' commands read below (connect,
	   begin, userinfo) reach the shared player module on the server side */
	if (sv_playercontext)
	{
		*sv_playercontext = 0;
	}

	/* keep the random time dependent */
	randk();

	/* check timeouts */
	SV_CheckTimeouts();

	/* get packets from clients */
	SV_ReadPackets();

	/* move autonomous things around if enough time has passed */
	if (!sv_timedemo->value && (svs.realtime < sv.time))
	{
		/* never let the time get too far off */
		if (sv.time - svs.realtime > 100)
		{
			if (sv_showclamp->value)
			{
				Com_Printf("sv lowclamp\n");
			}

			svs.realtime = sv.time - 100;
		}

		NET_Sleep(sv.time - svs.realtime);
		return;
	}

	/* update ping based on the last known frame from all clients */
	SV_CalcPings();

	/* give the clients some timeslices */
	SV_GiveMsec();

	/* let everything in the world think and move */
	if (sv.state == ss_game)
	{
		SV_RunGameFrame();
	}
	else
	{
		sv.framenum++;
		sv.time = sv.framenum * 100;
	}

	/* send messages back to the clients that had packets read this frame */
	SV_Watchdog_Mark("SV_SendClientMessages");
	SV_SendClientMessages();
	SV_SendPrepClientMessages();
	SV_Watchdog_Mark("SV_Frame end");

	/* save the entire world state if recording a serverdemo */
	SV_RecordDemoMessage();

	/* send a heartbeat to the master if needed */
	Master_Heartbeat();

	/* SPEC-202 s3.1: the events of all entities are cleared after every frame */
	SV_ClearEntityEvents();
}

/*
 * OpenMerc: GameSpy heartbeat to every master in sv_masters when the
 * player count changed, a new game started, "heartbeat" was given or
 * 150 s passed. Replaces the Quake II heartbeat to the id master.
 */
void
Master_Heartbeat(void)
{
	SV_GameSpy_CheckHeartbeat();
}

/*
 * Pull specific info from a newly changed userinfo string
 * into a more C freindly form.
 */
void
SV_UserinfoChanged(client_t *cl, qboolean ingame)
{
	char *val;
	int i;

	/* call prog code to allow overrides */
	ge->ClientUserinfoChanged(CL_EDICT(cl), cl->userinfo, ingame ? 1 : 0);

	/* name for C code (SPEC-203: up to 31 characters kept) */
	Q_strlcpy(cl->name, Info_ValueForKey(cl->userinfo, "name"), sizeof(cl->name));

	/* mask off high bit */
	for (i = 0; i < sizeof(cl->name); i++)
	{
		cl->name[i] &= 127;
	}

	/* rate command */
	val = Info_ValueForKey(cl->userinfo, "rate");

	if (strlen(val))
	{
		cl->rate = Q_clamp((int)strtol(val, (char **)NULL, 10), 100, 15000);
	}
	else
	{
		cl->rate = 5000;
	}

	/* msg command */
	val = Info_ValueForKey(cl->userinfo, "msg");

	if (strlen(val))
	{
		cl->messagelevel = (int)strtol(val, (char **)NULL, 10);
	}
}

/*
 * Only called at startup, not for each game
 */
void
SV_Init(void)
{
	SV_InitOperatorCommands();
	SV_InitTables();
	SV_SP_Init();
	SV_InitSaveHost();
	SV_Watchdog_Init();

	/* the Ghoul library serves every module from here on (SPEC-010) */
	SV_Ghoul_Init();

	rcon_password = Cvar_Get("rcon_password", "", 0);
	Cvar_Get("skill", "2", CVAR_LATCH);
	Cvar_Get("deathmatch", "0", CVAR_LATCH);
	Cvar_Get("dmflags", "0", CVAR_SERVERINFO | CVAR_LATCH);
	Cvar_Get("fraglimit", "0", CVAR_SERVERINFO);
	Cvar_Get("timelimit", "0", CVAR_SERVERINFO);
	Cvar_Get("cheats", "0", CVAR_SERVERINFO | CVAR_LATCH);
	Cvar_Get("protocol", va("%i", SV_PROTOCOL_VERSION), CVAR_SERVERINFO | CVAR_NOSET);
	Cvar_Get("version", "1.07fx86F", CVAR_SERVERINFO | CVAR_NOSET);
	Cvar_Get("welcome_mess", "Welcome to Soldier of Fortune Multiplayer", CVAR_ARCHIVE);
	Cvar_Get("sv_restartlevel", "0", 0);
	sv_jumpcinematic = Cvar_Get("sv_jumpcinematic", "0", 0);
	maxclients = Cvar_Get("maxclients", "1", CVAR_SERVERINFO | CVAR_LATCH);
	hostname = Cvar_Get("hostname", "noname", CVAR_SERVERINFO | CVAR_ARCHIVE);
	timeout = Cvar_Get("timeout", "180", 0);
	zombietime = Cvar_Get("zombietime", "2", 0);
	sv_showclamp = Cvar_Get("showclamp", "0", 0);
	sv_paused = Cvar_Get("paused", "0", 0);
	sv_timedemo = Cvar_Get("timedemo", "0", 0);
	sv_enforcetime = Cvar_Get("sv_enforcetime", "0", 0);
	sv_reconnect_limit = Cvar_Get("sv_reconnect_limit", "3", CVAR_ARCHIVE);
	sv_max_ghoul_mp = Cvar_Get("sv_max_ghoul_mp", "200", 0);
	allow_download = Cvar_Get("allow_download", "0", CVAR_ARCHIVE);
	allow_download_maps = Cvar_Get("allow_download_maps", "1", CVAR_ARCHIVE);
	allow_download_sounds = Cvar_Get("allow_download_sounds", "1", CVAR_ARCHIVE);
	allow_download_stringpackage = Cvar_Get("allow_download_stringpackage", "1", CVAR_ARCHIVE);
	allow_download_players = Cvar_Get("allow_download_players", "0", CVAR_ARCHIVE);
	allow_download_models = Cvar_Get("allow_download_models", "1", CVAR_ARCHIVE);
	sv_noreload = Cvar_Get("sv_noreload", "0", 0);
	public_server = Cvar_Get("public", "0", 0);
	sv_entfile = Cvar_Get("sv_entfile", "0", 0);

	/* OpenMerc host options */
	sv_hostdebug = Cvar_Get("sv_hostdebug", "0", 0);
	sv_ghoul_stub = Cvar_Get("sv_ghoul_stub", "0", 0);

	/* GameSpy query socket and master list (OpenMerc, SPEC-204) */
	SV_GameSpy_Init();

	SZ_Init(&net_message, net_message_buffer, sizeof(net_message_buffer));
}

/*
 * Used by SV_Shutdown to send a final message to all connected clients
 * before the server goes down: twice, outside the reliable stream,
 * svc_print (level 2) and svc_reconnect (map change) or svc_disconnect
 * "Server Quit!" (SPEC-201 s8).
 */
void
SV_FinalMessage(const char *message, qboolean reconnect)
{
	int i, pass;
	client_t *cl;

	SZ_Clear(&net_message);
	MSG_WriteByte(&net_message, sofsvc_print);
	MSG_WriteByte(&net_message, PRINT_HIGH);
	MSG_WriteString(&net_message, message);

	if (reconnect)
	{
		MSG_WriteByte(&net_message, sofsvc_reconnect);
	}
	else
	{
		MSG_WriteByte(&net_message, sofsvc_disconnect);
		MSG_WriteShort(&net_message, SV_STR_SERVERQUIT);
	}

	/* stagger the packets to crutch operating system limited buffers */
	for (pass = 0; pass < 2; pass++)
	{
		for (i = 0, cl = svs.clients; i < svs.numclients; i++, cl++)
		{
			if ((cl->state >= cs_connected) && !SV_ClientIsBot(cl))
			{
				Netchan_Transmit(&cl->netchan, net_message.cursize,
						net_message.data);
			}
		}
	}
}

/*
 * Called when each game quits,
 * before Sys_Quit or Sys_Error
 */
void
SV_Shutdown(char *finalmsg, qboolean reconnect)
{
	if (svs.clients)
	{
		SV_FinalMessage(finalmsg, reconnect);
	}

	/* SPEC-010 lifetimes: drop every network client, the game's Shutdown,
	   then the reset unless only reconnecting */
	SV_ShutdownGameAndGhoul(svs.initialized, reconnect);
	SV_SP_Release();

	/* free current level */
	if (sv.demofile)
	{
		FS_FCloseFile(sv.demofile);
	}

	SV_FreeMapExtras();
	memset(&sv, 0, sizeof(sv));
	Com_SetServerState(sv.state);

	/* free server static data */
	if (svs.clients)
	{
		Z_Free(svs.clients);
	}

	if (svs.client_entities)
	{
		Z_Free(svs.client_entities);
	}

	if (svs.demofile)
	{
		fclose(svs.demofile);
	}

	memset(&svs, 0, sizeof(svs));
	net_maxmsglen = MAX_MSGLEN; /* SPEC-200: back to the network size */
}
