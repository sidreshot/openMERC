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
 * Server side user (player entity) moving.
 *
 * OpenMerc: the SoF 1.07f signon (new, configstrings with the dictionary
 * coding, ghoulstrings, baselines, sv_precache, begin), downloads, and
 * the client messages of protocol 33 (bit-packed user commands with the
 * MD4 move checksum, userinfo, string commands).
 *
 * Implements: SPEC-203 s2-s4, SPEC-201 (client -> server ids),
 *             SPEC-202 s6, SPEC-200 (cd value), SPEC-001 (Connect, Enter
 *             game, Commands, Movement)
 *
 * =======================================================================
 */

#include "header/server.h"

#define MAX_STRINGCMDS 8

edict_t *sv_player;

extern qboolean file_from_protected_pak; /* common/filesystem.c */

static qboolean
SV_SignonSpawnCount(const char *step)
{
	if ((Cmd_Argc() <= 1) ||
		((int)strtol(Cmd_Argv(1), (char **)NULL, 10) != svs.spawncount))
	{
		Com_Printf("SV_%s_f from different level\n", step);
		return false;
	}

	return true;
}

static void
SV_BeginDemoserver(void)
{
	char name[MAX_OSPATH];

	Com_sprintf(name, sizeof(name), "demos/%s", sv.name);
	FS_FOpenFile(name, &sv.demofile, false);

	if (!sv.demofile)
	{
		Com_Error(ERR_DROP, "Couldn't open %s\n", name);
	}
}

/*
 * Sends the first message from the server to a connected client.
 * This will be sent on the initial connection and upon each server load.
 */
static void
SV_New_f(void)
{
	const char *gamedir;
	int playernum;

	Com_DPrintf("New() from %s\n", sv_client->name);

	if (sv_client->state != cs_connected)
	{
		Com_Printf("New not valid -- already spawned\n");
		return;
	}

	/* demo servers just dump the file message */
	if (sv.state == ss_demo)
	{
		SV_BeginDemoserver();
		return;
	}

	/* SPEC-200: the local client must have sent a cd value of 3.14 */
	if ((sv_client->netchan.remote_address.type == NA_LOOPBACK) &&
		(sv_client->cdvalue * 0.4f < 1.98f))
	{
		SV_DropClientWithReason(sv_client, SV_STR_INSERTCD);
		return;
	}

	gamedir = Cvar_VariableString("gamedir");
	playernum = (int)(sv_client - svs.clients);

	/* send the serverdata (SPEC-203) */
	MSG_WriteByte(&sv_client->netchan.message, sofsvc_serverdata);
	MSG_WriteLong(&sv_client->netchan.message, SV_PROTOCOL_VERSION);
	MSG_WriteLong(&sv_client->netchan.message, svs.spawncount);
	MSG_WriteByte(&sv_client->netchan.message, sv.attractloop);
	MSG_WriteByte(&sv_client->netchan.message, (int)Cvar_VariableValue("deathmatch"));
	MSG_WriteString(&sv_client->netchan.message, gamedir);
	MSG_WriteShort(&sv_client->netchan.message, playernum);

	/* send full levelname */
	MSG_WriteString(&sv_client->netchan.message, sv.configstrings[SOF_CS_NAME]);

	/* game server */
	if (sv.state == ss_game)
	{
		/* set up the entity for the client */
		CLNUM_EDICT(playernum)->s.number = playernum + 1;
		memset(&sv_client->lastcmd, 0, sizeof(sv_client->lastcmd));

		/* the Ghoul system queues every server object for this client */
		if (!sv_client->ghoulknown)
		{
			SV_Ghoul_ClientConnected(playernum);
			sv_client->ghoulknown = true;
		}

		/* begin fetching configstrings */
		MSG_WriteByte(&sv_client->netchan.message, sofsvc_stufftext);
		MSG_WriteString(&sv_client->netchan.message,
				va("cmd configstrings %i 0\n", svs.spawncount));
	}
}

static void
SV_Configstrings_f(void)
{
	sizebuf_t *msg;
	int i, start, limit;
	qboolean sp;

	if (sv_client->state != cs_connected)
	{
		Com_Printf("configstrings not valid -- already spawned\n");
		return;
	}

	/* handle the case of a level changing while a client was connecting */
	if (!SV_SignonSpawnCount("Configstrings"))
	{
		SV_New_f();
		return;
	}

	start = (Cmd_Argc() > 2) ? (int)strtol(Cmd_Argv(2), (char **)NULL, 10) : 0;

	if (start < 0)
	{
		start = 0;
	}

	Com_DPrintf("Configstrings() from %s\n", sv_client->name);

	/* SPEC-203: single player skips the Ghoul file names (the OpenMerc sound extension after them is sent) */
	sp = !Cvar_VariableValue("deathmatch");
	limit = OM_MAX_CONFIGSTRINGS;
	msg = &sv_client->netchan.message;

	for (i = start; i < limit; i++)
	{
		if (sp && (i >= SOF_CS_GHOULFILES) && (i < OM_CS_SOUNDS_EXT))
		{
			i = OM_CS_SOUNDS_EXT;
		}

		if (msg->cursize >= SV_SIGNON_BATCH)
		{
			break;
		}

		if (sv.configstrings[i][0])
		{
			SV_WriteConfigstringMsg(msg, i, sv.configstrings[i]);
		}
	}

	/* send next command */
	MSG_WriteByte(msg, sofsvc_stufftext);

	if (i < limit)
	{
		MSG_WriteString(msg, va("cmd configstrings %i %i\n", svs.spawncount, i));
	}
	else if (sv.state == ss_demo)
	{
		MSG_WriteString(msg, va("cmd baselines %i 0\n", svs.spawncount));
	}
	else
	{
		MSG_WriteString(msg, va("cmd ghoulstrings %i 0\n", svs.spawncount));
	}
}

/* one chunk of the server's Ghoul registry (SPEC-203 s2) */
static void
SV_Ghoulstrings_f(void)
{
	sizebuf_t *msg;
	byte buf[SV_MAX_DATAGRAM];
	int n, budget, len, done = 1;

	if (sv_client->state != cs_connected)
	{
		Com_Printf("ghoulstrings not valid -- already spawned\n");
		return;
	}

	if (!SV_SignonSpawnCount("Ghoulstrings"))
	{
		SV_New_f();
		return;
	}

	n = (Cmd_Argc() > 2) ? (int)strtol(Cmd_Argv(2), (char **)NULL, 10) : 0;
	Com_DPrintf("Ghoulstrings() from %s\n", sv_client->name);

	msg = &sv_client->netchan.message;

	/* the chunk fills the remaining half-packet budget plus 8 bytes */
	budget = SV_SIGNON_BATCH - msg->cursize + 8;

	if (budget > SV_DATAGRAM_LIMIT - 64 - msg->cursize)
	{
		budget = SV_DATAGRAM_LIMIT - 64 - msg->cursize;
	}

	if (budget < 4)
	{
		budget = 4;
	}

	len = SV_Ghoul_WriteReliable((int)(sv_client - svs.clients), buf, budget, &done);

	MSG_WriteByte(msg, sofsvc_ghoulreliable);
	MSG_WriteShort(msg, len);
	SZ_Write(msg, buf, len);

	MSG_WriteByte(msg, sofsvc_stufftext);

	if (!done)
	{
		MSG_WriteString(msg, va("cmd ghoulstrings %i %i\n", svs.spawncount, n + 1));
	}
	else
	{
		MSG_WriteString(msg, va("cmd baselines %i 0\n", svs.spawncount));
	}
}

static void
SV_Baselines_f(void)
{
	sizebuf_t *msg;
	int start, i;

	if (sv_client->state != cs_connected)
	{
		Com_Printf("baselines not valid -- already spawned\n");
		return;
	}

	/* handle the case of a level changing while a client was connecting */
	if (!SV_SignonSpawnCount("Baselines"))
	{
		SV_New_f();
		return;
	}

	start = (Cmd_Argc() > 2) ? (int)strtol(Cmd_Argv(2), (char **)NULL, 10) : 0;

	if (start < 0)
	{
		start = 0;
	}

	Com_DPrintf("Baselines() from %s\n", sv_client->name);

	msg = &sv_client->netchan.message;

	for (i = start; i < SOF_MAX_EDICTS; i++)
	{
		if (msg->cursize >= SV_SIGNON_BATCH)
		{
			break;
		}

		if (SV_BaselineWanted(&sv.baselines[i]))
		{
			MSG_WriteByte(msg, sofsvc_spawnbaseline);
			SV_WriteDeltaEntity(NULL, &sv.baselines[i], msg, true, true);
		}
	}

	/* send next command */
	MSG_WriteByte(msg, sofsvc_stufftext);

	if (i < SOF_MAX_EDICTS)
	{
		MSG_WriteString(msg, va("cmd baselines %i %i\n", svs.spawncount, i));
	}
	else
	{
		MSG_WriteString(msg, va("precache %i\n", svs.spawncount));
	}
}

/* the client finished loading: tell it to begin (note the space) */
static void
SV_Precache_f(void)
{
	Com_DPrintf("SV_Precache_f() from %s\n", sv_client->name);

	if ((sv.state != ss_demo) && !SV_SignonSpawnCount("Precache"))
	{
		SV_New_f();
		return;
	}

	MSG_WriteByte(&sv_client->netchan.message, sofsvc_stufftext);
	MSG_WriteString(&sv_client->netchan.message, va("cmd begin %i \n", svs.spawncount));
}

static void
SV_Begin_f(void)
{
	Com_DPrintf("Begin() from %s\n", sv_client->name);

	if (sv_client->state != cs_connected)
	{
		Com_Printf("begin not valid -- already spawned\n");
		return;
	}

	/* handle the case of a level changing while a client was connecting */
	if (!SV_SignonSpawnCount("Begin"))
	{
		SV_New_f();
		return;
	}

	sv_client->state = cs_spawned;
	sv_client->entered = true;

	/* call the game begin function; force the shared player-module context to server
	   (0) so listen-server DeleteInventory does not treat stale view instances as client
	   ones after GhoulResetLevel (MODEB-GAPS crash B). Same rule as ClientThink. */
	SV_HostDebug(SV_DBG_THINK, "begin frame %i cin %i\n", sv.framenum, SV_GameCinematicState());
	if (sv_playercontext)
	{
		int saved = *sv_playercontext;

		*sv_playercontext = 0;
		ge->ClientBegin(sv_player);

		if (sv_playercontext)
		{
			*sv_playercontext = saved;
		}
	}
	else
	{
		ge->ClientBegin(sv_player);
	}
	SV_HostDebug(SV_DBG_THINK, "after begin: cin %i\n", SV_GameCinematicState());

	Cbuf_InsertFromDefer();
}

static void
SV_NextDownload_f(void)
{
	int r;
	int percent;
	int size;

	if (!sv_client->download)
	{
		return;
	}

	r = sv_client->downloadsize - sv_client->downloadcount;

	if (r > 1024)
	{
		r = 1024;
	}

	MSG_WriteByte(&sv_client->netchan.message, sofsvc_download);
	MSG_WriteShort(&sv_client->netchan.message, r);

	sv_client->downloadcount += r;
	size = sv_client->downloadsize;

	if (!size)
	{
		size = 1;
	}

	percent = sv_client->downloadcount * 100 / size;
	MSG_WriteByte(&sv_client->netchan.message, percent);
	SZ_Write(&sv_client->netchan.message,
			sv_client->download + sv_client->downloadcount - r, r);

	if (sv_client->downloadcount != sv_client->downloadsize)
	{
		return;
	}

	FS_FreeFile(sv_client->download);
	sv_client->download = NULL;
}

static void
SV_DownloadRefused(void)
{
	MSG_WriteByte(&sv_client->netchan.message, sofsvc_download);
	MSG_WriteShort(&sv_client->netchan.message, -1);
	MSG_WriteByte(&sv_client->netchan.message, 0);
}

/* SPEC-203 s4: which names may be served */
static qboolean
SV_DownloadAllowed(const char *name)
{
	if (strstr(name, "..") || (name[0] == '.') || (name[0] == '/') || !strchr(name, '/'))
	{
		return false;
	}

	if (!Q_strncasecmp(name, "strip/", 6))
	{
		return allow_download_stringpackage->value && allow_download->value;
	}

	if (!Q_strncasecmp(name, "sound/", 6))
	{
		return allow_download_sounds->value && allow_download->value;
	}

	if (!Q_strncasecmp(name, "maps/", 5) || !Q_strncasecmp(name, "bot/", 4))
	{
		return allow_download_maps->value && allow_download->value;
	}

	return true; /* observed: other paths ignore allow_download */
}

static void
SV_BeginDownload_f(void)
{
	char *name;
	int offset = 0;

	name = Cmd_Argv(1);

	if (Cmd_Argc() > 2)
	{
		offset = (int)strtol(Cmd_Argv(2), (char **)NULL, 10); /* downloaded offset */
	}

	if (!SV_DownloadAllowed(name))
	{
		SV_DownloadRefused();
		return;
	}

	if (sv_client->download)
	{
		FS_FreeFile(sv_client->download);
	}

	sv_client->downloadsize = FS_LoadFile(name, (void **)&sv_client->download);
	sv_client->downloadcount = offset;

	if (offset > sv_client->downloadsize)
	{
		sv_client->downloadcount = sv_client->downloadsize;
	}

	if (!sv_client->download ||
		(!Q_strncasecmp(name, "maps/", 5) && file_from_protected_pak))
	{
		Com_DPrintf("Couldn't download %s to %s\n", name, sv_client->name);

		if (sv_client->download)
		{
			FS_FreeFile(sv_client->download);
			sv_client->download = NULL;
		}

		SV_DownloadRefused();
		return;
	}

	SV_NextDownload_f();
	Com_DPrintf("Downloading %s to %s\n", name, sv_client->name);
}

/*
 * The client is going to disconnect, so remove the connection immediately
 */
static void
SV_Disconnect_f(void)
{
	SV_DropClientWithReason(sv_client, SV_STR_DISCONNECTED);
}

/*
 * Dumps the serverinfo info string
 */
static void
SV_ShowServerinfo_f(void)
{
	Info_Print(Cvar_Serverinfo());
}

void
SV_Nextserver(void)
{
	const char *v;

	if (sv.state == ss_game)
	{
		return; /* can't nextserver while playing a normal game */
	}

	svs.spawncount++; /* make sure another doesn't sneak in */
	v = Cvar_VariableString("nextserver");

	if (!v[0])
	{
		Cbuf_AddText("killserver\n");
	}
	else
	{
		Cbuf_AddText((char *)v);
		Cbuf_AddText("\n");
	}

	Cvar_Set("nextserver", "");
}

/*
 * A cinematic has completed or been aborted by a client, so move
 * to the next server,
 */
static void
SV_Nextserver_f(void)
{
	if ((int)strtol(Cmd_Argv(1), (char **)NULL, 10) != svs.spawncount)
	{
		Com_DPrintf("Nextserver() from wrong level, from %s\n", sv_client->name);
		return; /* leftover from last server */
	}

	Com_DPrintf("Nextserver() from %s\n", sv_client->name);

	SV_Nextserver();
}

typedef struct
{
	char *name;
	void (*func)(void);
} ucmd_t;

static ucmd_t ucmds[] = {
	/* auto issued */
	{"new", SV_New_f},
	{"configstrings", SV_Configstrings_f},
	{"ghoulstrings", SV_Ghoulstrings_f},
	{"baselines", SV_Baselines_f},
	{"sv_precache", SV_Precache_f},
	{"begin", SV_Begin_f},
	{"nextserver", SV_Nextserver_f},
	{"disconnect", SV_Disconnect_f},

	/* issued by hand at client consoles */
	{"info", SV_ShowServerinfo_f},

	{"download", SV_BeginDownload_f},
	{"nextdl", SV_NextDownload_f},

	{NULL, NULL}
};

void
SV_ExecuteUserCommand(char *s)
{
	ucmd_t *u;

	/* clients can't macro expand variables on the server */
	Cmd_TokenizeString(s, false);
	sv_player = CL_EDICT(sv_client);

	for (u = ucmds; u->name; u++)
	{
		if (!strcmp(Cmd_Argv(0), u->name))
		{
			u->func();
			break;
		}
	}

	if (!u->name && (sv.state == ss_game))
	{
		SV_GameClientCommand(sv_player);
	}
}

/* ---------------------------------------------------------------------
 * user commands (SPEC-202 s6.2)
 * --------------------------------------------------------------------- */

typedef struct
{
	const byte *data;
	int bits, bit;
	qboolean empty;
} bitreader_t;

static unsigned
SV_ReadBits(bitreader_t *b, int n)
{
	unsigned v = 0;
	int i;

	for (i = 0; i < n; i++)
	{
		if (b->bit >= b->bits)
		{
			b->empty = true;
			return v;
		}

		if (b->data[b->bit >> 3] & (1 << (b->bit & 7)))
		{
			v |= 1u << i;
		}

		b->bit++;
	}

	return v;
}

/* one command coded against `from' (the receiver keeps unread fields) */
static void
SV_ReadUsercmd(bitreader_t *b, const sof_usercmd_t *from, sof_usercmd_t *to)
{
	static const int limit[3] = {200, 160, 200};
	int16_t *moves[3];
	unsigned v;
	int i;

	*to = *from;
	moves[0] = &to->forwardmove;
	moves[1] = &to->sidemove;
	moves[2] = &to->upmove;

	for (i = 0; i < 3 && !b->empty; i++)
	{
		if (SV_ReadBits(b, 1))
		{
			v = SV_ReadBits(b, 12);

			if (!b->empty)
			{
				to->angles[i] = (int16_t)(v << 4);
			}
		}
	}

	for (i = 0; i < 3 && !b->empty; i++)
	{
		if (SV_ReadBits(b, 1))
		{
			v = SV_ReadBits(b, 10);

			if (!b->empty)
			{
				*moves[i] = (int16_t)Q_clamp((int)v - 510, -limit[i], limit[i]);
			}
		}
	}

	if (!b->empty && SV_ReadBits(b, 1))
	{
		v = SV_ReadBits(b, 8);

		if (!b->empty)
		{
			to->buttons = (uint8_t)v;
		}
	}

	if (!b->empty)
	{
		/* lean is always sent */
		if (SV_ReadBits(b, 1))
		{
			to->lean = SV_ReadBits(b, 1) ? -1 : 1;
		}
		else if (!b->empty)
		{
			to->lean = 0;
		}
	}

	if (!b->empty && SV_ReadBits(b, 1))
	{
		v = SV_ReadBits(b, 5);

		if (!b->empty)
		{
			to->lightlevel = (uint8_t)(v << 3);
		}
	}

	if (!b->empty)
	{
		v = SV_ReadBits(b, 8);

		if (!b->empty)
		{
			to->msec = (uint8_t)v;
		}
	}

	if (!b->empty && SV_ReadBits(b, 1))
	{
		v = SV_ReadBits(b, 32);

		if (!b->empty)
		{
			memcpy(&to->firetime, &v, 4);
		}
	}

	if (!b->empty && SV_ReadBits(b, 1))
	{
		v = SV_ReadBits(b, 32);

		if (!b->empty)
		{
			memcpy(&to->altfiretime, &v, 4);
		}
	}
}

static void
SV_ClientThink(client_t *cl, sof_usercmd_t *cmd)
{
	cl->commandMsec -= cmd->msec;

	if ((cl->commandMsec < 0) && sv_enforcetime->value)
	{
		Com_DPrintf("commandMsec underflow from %s\n", cl->name);
		return;
	}

	SV_GameClientThink(CL_EDICT(cl), cmd);
}

/*
 * The current net_message is parsed for the given client
 */
void
SV_ExecuteClientMessage(client_t *cl)
{
	int c;
	char *s;
	sof_usercmd_t nullcmd;
	sof_usercmd_t oldest, oldcmd, newcmd;
	int net_drop;
	int stringCmdCount;
	int checksum, calculatedChecksum;
	int checksumIndex;
	qboolean move_issued;
	int lastframe;
	bitreader_t br;

	sv_client = cl;
	sv_player = CL_EDICT(sv_client);

	/* only allow one move command */
	move_issued = false;
	stringCmdCount = 0;

	while (1)
	{
		if (net_message.readcount > net_message.cursize)
		{
			Com_Printf("SV_ReadClientMessage: bad read\n");
			SV_DropClientWithReason(cl, SV_STR_BADREAD);
			return;
		}

		c = MSG_ReadByte(&net_message);

		if (c == -1)
		{
			break;
		}

		switch (c)
		{
			default:
				Com_Printf("SV_ReadClientMessage: unknown command char\n");
				SV_DropClientWithReason(cl, SV_STR_BADCOMMAND);
				return;

			case clc_nop:
				break;

			case clc_userinfo:
				Q_strlcpy(cl->userinfo, MSG_ReadString(&net_message), 512);
				SV_UserinfoChanged(cl, true);
				break;

			case clc_move:

				if (move_issued)
				{
					return; /* only the first move in a packet counts */
				}

				move_issued = true;
				checksumIndex = net_message.readcount;
				checksum = MSG_ReadByte(&net_message);
				lastframe = MSG_ReadLong(&net_message);

				if (lastframe != cl->lastframe)
				{
					cl->lastframe = lastframe;

					if (cl->lastframe > 0)
					{
						cl->frame_latency[cl->lastframe & (LATENCY_COUNTS - 1)] =
							svs.realtime - cl->frames[cl->lastframe & UPDATE_MASK].senttime;
					}
				}

				/* the three commands are one bit stream padded to bytes */
				br.data = net_message.data + net_message.readcount;
				br.bits = (net_message.cursize - net_message.readcount) * 8;
				br.bit = 0;
				br.empty = false;

				memset(&nullcmd, 0, sizeof(nullcmd));
				SV_ReadUsercmd(&br, &nullcmd, &oldest);
				SV_ReadUsercmd(&br, &oldest, &oldcmd);
				SV_ReadUsercmd(&br, &oldcmd, &newcmd);

				net_message.readcount += (br.bit + 7) >> 3;

				if (cl->state != cs_spawned)
				{
					cl->lastframe = -1;
					break;
				}

				/* if the checksum fails, ignore the rest of the packet */
				calculatedChecksum = SV_MoveChecksum(
					net_message.data + checksumIndex + 1,
					net_message.readcount - checksumIndex - 1,
					cl->netchan.incoming_sequence);

				if (calculatedChecksum != checksum)
				{
					Com_DPrintf("Failed command checksum for %s (%d != %d)/%d\n",
							cl->name, calculatedChecksum, checksum,
							cl->netchan.incoming_sequence);
					return;
				}

				/* the Ghoul delta bases follow the acknowledged frame */
				SV_Ghoul_Acknowledge((int)(cl - svs.clients), lastframe);

				if (!sv_paused->value)
				{
					net_drop = cl->netchan.dropped;

					if (net_drop < 20)
					{
						while (net_drop > 2)
						{
							SV_ClientThink(cl, &cl->lastcmd);
							net_drop--;
						}

						if (net_drop > 1)
						{
							SV_ClientThink(cl, &oldest);
						}

						if (net_drop > 0)
						{
							SV_ClientThink(cl, &oldcmd);
						}
					}

					SV_ClientThink(cl, &newcmd);
				}

				cl->lastcmd = newcmd;
				break;

			case clc_stringcmd:
				s = MSG_ReadString(&net_message);

				/* only the first 7 per packet are executed */
				if (++stringCmdCount < MAX_STRINGCMDS)
				{
					SV_ExecuteUserCommand(s);
				}

				if (cl->state == cs_zombie)
				{
					return; /* disconnect command */
				}

				break;
		}
	}
}
