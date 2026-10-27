/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Soldier of Fortune 1.07f client demos (.dm2): recording with `record` /
 * `stop` and playback with `demomap`.
 *
 * A demo is the recorded server -> client message stream in blocks of
 * {int32 length, message bytes}, ended by length -1. The recorder writes a
 * header (svc_serverdata with the demo flag, every configstring in the
 * dictionary coding, the baselines as the server sent them, the precache
 * stufftext), waits for an uncompressed frame, writes the stored reliable
 * ghoul chunks and then every parsed message.
 *
 * The original plays demos through its local server, which sends one block
 * per server frame (100 ms) to the client. OpenMerc plays them inside the
 * client with the same timing (one block per 100 ms, one per client frame
 * with timedemo), so playback does not depend on the server code; the client
 * treats the stream exactly like one from a server.
 *
 * Implements: SPEC-311, SPEC-310 section 9 (menu_nostats)
 */

#include "header/client.h"
#include "header/sof_tables.h"
#include "header/sofclient.h"
#include "../common/header/sof/engine.h" /* FS_Userdir */

/* message ids used by the recorder (SPEC-201) */
#define DSVC_STUFFTEXT 13
#define DSVC_SERVERDATA 14
#define DSVC_CONFIGSTRING 15
#define DSVC_SPAWNBASELINE 16
#define DSVC_GHOULRELIABLE 26

#define DEMO_PROTOCOL 33
#define DEMO_MAX_CONFIGSTRINGS 3711 /* with the OpenMerc sound extension (game_api.h) */
#define DEMO_MAX_EDICTS 1024
#define DEMO_SP_MSGLEN 16384  /* single-player / loopback message size */
#define DEMO_NET_MSGLEN 1400  /* network message size */
#define DEMO_BASELINE_MAX 96

/* ---- recording state -------------------------------------------------- */

typedef struct
{
	short len;
	byte data[DEMO_BASELINE_MAX];
} demobaseline_t;

static demobaseline_t baselines[DEMO_MAX_EDICTS];

/* stored reliable ghoul stream: records of {int32 L, L bytes} */
static byte *ghoulstore;
static int ghoulstore_len, ghoulstore_size;

/* ---- playback state ---------------------------------------------------- */

static qboolean playing;
static fileHandle_t demohandle;
static int demo_remaining;
static int next_block_time;
static byte playbuf[DEMO_SP_MSGLEN];
static char playname[MAX_OSPATH];

static cvar_t *menu_nostats;

qboolean
CL_Demo_Playing(void)
{
	return playing;
}

/* ---------------------------------------------------------------------
 * recorder hooks (called by the protocol-33 parser)
 * --------------------------------------------------------------------- */

void
CL_Demo_ClearLevel(void)
{
	memset(baselines, 0, sizeof(baselines));
	ghoulstore_len = 0;
}

void
CL_Demo_StoreBaseline(int number, const byte *data, int len)
{
	if ((number < 0) || (number >= DEMO_MAX_EDICTS) || (len <= 0) || (len > DEMO_BASELINE_MAX))
	{
		return;
	}

	baselines[number].len = (short)len;
	memcpy(baselines[number].data, data, len);
}

void
CL_Demo_StoreGhoulReliable(const byte *data, int len)
{
	int need, l;

	if (len < 0)
	{
		return;
	}

	need = ghoulstore_len + 4 + len;

	if (need > ghoulstore_size)
	{
		int size = ghoulstore_size ? ghoulstore_size : 65536;
		byte *n;

		while (size < need)
		{
			size *= 2;
		}

		n = realloc(ghoulstore, size);

		if (!n)
		{
			return;
		}

		ghoulstore = n;
		ghoulstore_size = size;
	}

	l = LittleLong(len);
	memcpy(ghoulstore + ghoulstore_len, &l, 4);
	memcpy(ghoulstore + ghoulstore_len + 4, data, len);
	ghoulstore_len += 4 + len;
}

static void
CL_Demo_WriteBlock(const byte *data, int len)
{
	int l = LittleLong(len);

	if (!cls.demofile)
	{
		return;
	}

	fwrite(&l, 4, 1, cls.demofile);

	if (len > 0)
	{
		fwrite(data, len, 1, cls.demofile);
	}
}

/* the first uncompressed frame: the stored ghoul stream goes first, then
   recording of messages starts (SPEC-311 client demo contents 2 and 3) */
void
CL_Demo_UncompressedFrame(void)
{
	byte rec[3 + 65536];
	int pos, l;

	if (cls.demorecording && cls.demowaiting)
	{
		for (pos = 0; pos + 4 <= ghoulstore_len; pos += 4 + l)
		{
			memcpy(&l, ghoulstore + pos, 4);
			l = LittleLong(l);

			if ((l < 0) || (pos + 4 + l > ghoulstore_len) || (l > 65535))
			{
				break;
			}

			rec[0] = DSVC_GHOULRELIABLE;
			rec[1] = (byte)(l & 0xFF);
			rec[2] = (byte)((l >> 8) & 0xFF);
			memcpy(rec + 3, ghoulstore + pos + 4, l);
			CL_Demo_WriteBlock(rec, l + 3);
		}
	}

	cls.demowaiting = false;
}

/* every parsed server message, after parsing (SPEC-311 contents 4) */
void
CL_Demo_AfterMessage(void)
{
	if (!cls.demorecording || cls.demowaiting || playing)
	{
		return;
	}

	/* the first eight bytes are the netchan header (SPEC-200) */
	if (net_message.cursize > 8)
	{
		CL_Demo_WriteBlock(net_message.data + 8, net_message.cursize - 8);
	}
}

/* Quake II name kept for other client code */
void
CL_WriteDemoMessage(void)
{
	CL_Demo_AfterMessage();
}

/* ---------------------------------------------------------------------
 * record / stop
 * --------------------------------------------------------------------- */

/* dictionary code of a configstring text, -1 = literal (SPEC-203); OpenMerc has no dictionary
   (docs/DEVIATIONS.md "Fixed tables") */
static int
CL_Demo_DictCode(const char *s)
{
	(void)s;
	return -1;
}

static void
CL_Demo_FlushHeader(sizebuf_t *buf)
{
	CL_Demo_WriteBlock(buf->data, buf->cursize);
	SZ_Clear(buf);
}

static int
CL_Demo_MaxMsgLen(void)
{
	/* the client's current maximum message size (SPEC-200) */
	return (Com_ServerState() && (Cvar_VariableValue("maxclients") <= 1)) ?
		DEMO_SP_MSGLEN : DEMO_NET_MSGLEN;
}

static void
CL_Demo_Record_f(void)
{
	char name[MAX_OSPATH], path[MAX_OSPATH];
	byte buf_data[DEMO_SP_MSGLEN];
	sizebuf_t buf;
	const char *s;
	int i, code;

	if (Cmd_Argc() != 2)
	{
		Com_Printf("record <demoname>\n");
		return;
	}

	if (cls.demorecording)
	{
		Com_Printf("Already recording.\n");
		return;
	}

	if ((cls.state != ca_active) || playing || !CL_SoF_Active())
	{
		Com_Printf("You must be in a level to record.\n");
		return;
	}

	/* <user dir>/demos/<name>, lower case; .dm2 unless contained */
	Q_strlcpy(name, Cmd_Argv(1), sizeof(name));
	Q_strlwr(name);

	if (!strstr(name, ".dm2"))
	{
		Q_strlcat(name, ".dm2", sizeof(name));
	}

	Com_sprintf(path, sizeof(path), "%s/demos/%s", FS_Userdir(), name);

	Com_Printf("Recording to %s.\n", path);
	FS_CreatePath(path);
	cls.demofile = Q_fopen(path, "wb");

	if (!cls.demofile)
	{
		Com_Printf("ERROR: couldn't open.\n");
		return;
	}

	cls.demorecording = true;

	/* don't record until a non-delta message is received */
	cls.demowaiting = true;

	SZ_Init(&buf, buf_data, CL_Demo_MaxMsgLen());
	buf.allowoverflow = true; /* a baseline larger than the 64-byte margin */

	/* serverdata with the demo flag */
	MSG_WriteByte(&buf, DSVC_SERVERDATA);
	MSG_WriteLong(&buf, DEMO_PROTOCOL);
	MSG_WriteLong(&buf, cl.servercount);
	MSG_WriteByte(&buf, 1);
	MSG_WriteByte(&buf, (int)Cvar_VariableValue("deathmatch"));
	MSG_WriteString(&buf, cl.gamedir);
	MSG_WriteShort(&buf, cl.playernum);
	MSG_WriteString(&buf, (char *)CL_SoF_ConfigString(0));

	/* configstrings */
	for (i = 0; i < DEMO_MAX_CONFIGSTRINGS; i++)
	{
		s = CL_SoF_ConfigString(i);

		if (!s[0])
		{
			continue;
		}

		if (buf.cursize + (int)strlen(s) + 32 > buf.maxsize)
		{
			CL_Demo_FlushHeader(&buf);
		}

		code = CL_Demo_DictCode(s);
		MSG_WriteByte(&buf, DSVC_CONFIGSTRING);
		MSG_WriteShort(&buf, i);
		MSG_WriteShort(&buf, code);

		if (code < 0)
		{
			MSG_WriteString(&buf, (char *)s);
		}
	}

	/* baselines, as the server coded them against the zero state */
	for (i = 0; i < DEMO_MAX_EDICTS; i++)
	{
		if (!baselines[i].len)
		{
			continue;
		}

		if (buf.cursize + 64 > buf.maxsize)
		{
			CL_Demo_FlushHeader(&buf);
		}

		MSG_WriteByte(&buf, DSVC_SPAWNBASELINE);
		SZ_Write(&buf, baselines[i].data, baselines[i].len);
	}

	MSG_WriteByte(&buf, DSVC_STUFFTEXT);
	MSG_WriteString(&buf, va("precache %i; reset_predn\n", cl.servercount));

	CL_Demo_FlushHeader(&buf);
}

void
CL_Stop_f(void)
{
	int len = -1;

	if (!cls.demorecording)
	{
		Com_Printf("Not recording a demo.\n");
		return;
	}

	fwrite(&len, 4, 1, cls.demofile);
	fclose(cls.demofile);
	cls.demofile = NULL;
	cls.demorecording = false;
	Com_Printf("Stopped demo.\n");
}

/* ---------------------------------------------------------------------
 * playback (demomap)
 * --------------------------------------------------------------------- */

qboolean
CL_Demo_StopPlayback(void)
{
	if (!playing)
	{
		return false;
	}

	playing = false;

	if (demohandle)
	{
		FS_FCloseFile(demohandle);
		demohandle = 0;
	}

	CL_SoF_SetDemoProtocol(false);
	return true;
}

static void
CL_Demo_Map_f(void)
{
	char name[MAX_OSPATH], path[MAX_OSPATH];
	netadr_t adr;
	size_t l;
	int len;

	if (Cmd_Argc() != 2)
	{
		Com_Printf("USAGE: demomap <demoname.dm2>\n");
		return;
	}

	Q_strlcpy(name, Cmd_Argv(1), sizeof(name));
	Q_strlwr(name);
	l = strlen(name);

	if ((l < 4) || strcmp(name + l - 4, ".dm2"))
	{
		Q_strlcat(name, ".dm2", sizeof(name));
	}

	Com_sprintf(path, sizeof(path), "demos/%s", name);

	if (cls.state != ca_disconnected)
	{
		CL_Disconnect();
	}

	if (Com_ServerState())
	{
		Cbuf_AddText("killserver\n");
	}

	/* menu cvar and loading screen (SPEC-310 section 9) */
	if (!menu_nostats)
	{
		menu_nostats = Cvar_Get("menu_nostats", "0", 0);
	}

	Cvar_Set("menu_nostats", "1");
	Com_Printf("Demo %s\n", name);
	SCR_BeginLoadingPlaque();

	len = FS_FOpenFile(path, &demohandle, false);

	if (!demohandle || (len < 0))
	{
		demohandle = 0;
		SCR_EndLoadingPlaque();
		Com_Error(ERR_DROP, "Couldn't open %s", path);
		return;
	}

	Q_strlcpy(playname, name, sizeof(playname));
	demo_remaining = len;

	/* a connected client without a server: the stream comes from the file */
	CL_ClearState();
	memset(&adr, 0, sizeof(adr));
	adr.type = NA_LOOPBACK;
	Netchan_Setup(NS_CLIENT, &cls.netchan, adr, cls.quakePort);
	Q_strlcpy(cls.servername, "demo", sizeof(cls.servername));
	cls.state = ca_connected;
	cls.key_dest = key_game;
	CL_SoF_SetDemoProtocol(true);

	playing = true;
	next_block_time = cls.realtime;
	cl.timedemo_frames = 0;
	cl.timedemo_start = 0;
}

/* the demo ended: the session ends like after `killserver`. OpenMerc test
   aid (not in the original): cl_omdemoquit 1 quits afterwards. */
static void
CL_Demo_End(void)
{
	CL_Disconnect();

	if (Cvar_VariableValue("cl_omdemoquit"))
	{
		Cbuf_AddText("quit\n");
	}
}

/* one block per server frame (100 ms); one per client frame with timedemo */
void
CL_Demo_RunFrame(void)
{
	sizebuf_t saved;
	int len, r;

	if (!playing)
	{
		return;
	}

	cls.netchan.last_received = curtime; /* no connection timeout */

	if (cl_paused && cl_paused->value)
	{
		next_block_time = cls.realtime;
		return;
	}

	if (!cl_timedemo->value)
	{
		if (cls.realtime < next_block_time)
		{
			return;
		}

		next_block_time += 100;

		/* do not try to catch up more than a frame after a stall */
		if (next_block_time < cls.realtime - 100)
		{
			next_block_time = cls.realtime;
		}
	}

	if (demo_remaining < 4)
	{
		CL_Demo_End();
		return;
	}

	r = FS_Read(&len, 4, demohandle);
	demo_remaining -= 4;
	len = LittleLong(len);

	if ((r != 4) || (len == -1))
	{
		CL_Demo_End();
		return;
	}

	if ((len < 0) || (len > DEMO_SP_MSGLEN))
	{
		CL_Demo_StopPlayback();
		Com_Error(ERR_DROP, "SV_SendClientMessages: msglen > MaxMsgLen");
		return;
	}

	if (len > demo_remaining)
	{
		CL_Demo_End(); /* short read */
		return;
	}

	r = FS_Read(playbuf, len, demohandle);
	demo_remaining -= len;

	if (r != len)
	{
		CL_Demo_End();
		return;
	}

	saved = net_message;
	net_message.data = playbuf;
	net_message.maxsize = sizeof(playbuf);
	net_message.cursize = len;
	net_message.readcount = 0;
	net_message.allowoverflow = false;
	net_message.overflowed = false;

	CL_ParseServerMessage();

	net_message = saved;
	net_message.cursize = 0;
	net_message.readcount = 0;

	/* whatever the client queued for the server is dropped */
	SZ_Clear(&cls.netchan.message);
}

/* ---------------------------------------------------------------------
 * init
 * --------------------------------------------------------------------- */

static void
CL_Demo_ResetPredn_f(void)
{
	/* weapon prediction reset (SPEC-003); no client weapon prediction yet */
}

void
CL_Demo_Init(void)
{
	Cmd_AddCommand("record", CL_Demo_Record_f);
	Cmd_AddCommand("stop", CL_Stop_f);

	/* the server registers demomap first; the client plays demos itself */
	if (Cmd_Exists("demomap"))
	{
		Cmd_RemoveCommand("demomap");
	}

	Cmd_AddCommand("demomap", CL_Demo_Map_f);

	if (!Cmd_Exists("reset_predn"))
	{
		Cmd_AddCommand("reset_predn", CL_Demo_ResetPredn_f);
	}

	menu_nostats = Cvar_Get("menu_nostats", "0", 0);
}
