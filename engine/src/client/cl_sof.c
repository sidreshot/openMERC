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
 * OpenMerc: client side of the Soldier of Fortune 1.07f network protocol
 * (protocol 33), used to join original SoF servers. Message layouts come
 * from the OpenMerc specs only; the frame / delta-entity bookkeeping
 * mirrors cl_parse.c (Quake II GPL release) so the rest of the client
 * (interpolation, prediction, view) works unchanged.
 *
 * Implements: SPEC-200, SPEC-201, SPEC-202, SPEC-203
 *
 * OpenMerc (I1a): svc_sound, svc_sound_info, the sound configstrings and
 * the player-state music/ambient values go to the sound module host
 * (snd_host.c). Implements: SPEC-004
 *
 * OpenMerc (I6h): the SoF player-state fields are kept per frame for the
 * view and HUD (sofps_t, cl_view.c); layouts, countdown, typewriter and
 * caption prints, name colours and the ignore list go to the HUD; baselines,
 * reliable ghoul data and the first uncompressed frame feed the demo
 * recorder (cl_demo.c). Implements: SPEC-310, SPEC-311; SPEC-AMEND-005 I6h.5 (loading progress per configstring)
 *
 * =======================================================================
 */

#include <time.h>

#include "header/client.h"
#include "header/sof_tables.h"
#include "header/sof_sound.h"
#include "header/sofclient.h" /* OpenMerc (I6h) */
#include "input/header/input.h"
#include "fx/cl_fx.h" /* OpenMerc (I5): effects, temp entities */
#include "header/cl_ghoul.h" /* I1c: the Ghoul client system */
#include "header/cl_player.h" /* I1d: client side of the player module */

/* ---------------------------------------------------------------------
 * protocol constants (SPEC-201, SPEC-202, SPEC-203)
 * --------------------------------------------------------------------- */

#define SOF_PROTOCOL_VERSION 33

enum
{
	sofsvc_bad, sofsvc_temp_entity, sofsvc_layout, sofsvc_unused, sofsvc_sound_info,
	sofsvc_effect, sofsvc_equip, sofsvc_nop, sofsvc_disconnect, sofsvc_reconnect,
	sofsvc_sound, sofsvc_print, sofsvc_nameprint, sofsvc_stufftext, sofsvc_serverdata,
	sofsvc_configstring, sofsvc_spawnbaseline, sofsvc_centerprint, sofsvc_captionprint,
	sofsvc_download, sofsvc_playerinfo, sofsvc_packetentities, sofsvc_deltapacketentities,
	sofsvc_frame, sofsvc_culledevent, sofsvc_damagetexture, sofsvc_ghoulreliable,
	sofsvc_ghoulunreliable, sofsvc_ric, sofsvc_restart_predn, sofsvc_rebuild_pred_inv,
	sofsvc_countdown, sofsvc_cinprintf, sofsvc_playernamecols, sofsvc_sp_print,
	sofsvc_removeconfigstring, sofsvc_sp_print_data_1, sofsvc_sp_print_data_2,
	sofsvc_welcomeprint, sofsvc_sp_print_obit, sofsvc_force_con_notify,
	SOFSVC_COUNT
};

static const char *sofsvc_names[SOFSVC_COUNT] = {
	"svc_bad", "svc_temp_entity", "svc_layout", "svc_UNUSED", "svc_sound_info",
	"svc_effect", "svc_equip", "svc_nop", "svc_disconnect", "svc_reconnect",
	"svc_sound", "svc_print", "svc_nameprint", "svc_stufftext", "svc_serverdata",
	"svc_configstring", "svc_spawnbaseline", "svc_centerprint", "svc_captionprint",
	"svc_download", "svc_playerinfo", "svc_packetentities", "svc_deltapacketentities",
	"svc_frame", "svc_culledEvent", "svc_damagetexture", "svc_ghoulreliable",
	"svc_ghoulunreliable", "svc_ric", "svc_restart_predn", "svc_rebuild_pred_inv",
	"svc_countdown", "svc_cinprintf", "svc_playernamecols", "svc_sp_print",
	"svc_removeconfigstring", "svc_sp_print_data_1", "svc_sp_print_data_2",
	"svc_welcomeprint", "svc_sp_print_obit", "svc_force_con_notify"
};

/* configstring layout (SPEC-203) */
#define SOF_MAX_CONFIGSTRINGS 3711 /* 1.07f's 3555 + the OpenMerc sound extension (game_api.h OM_MAX_SOUNDS) */
#define SOF_CS_LEN 64
#define SOF_CS_MAPCHECK 6
#define SOF_CS_MAXCLIENTS 31
#define SOF_CS_MODELS 35
#define SOF_CS_SOUNDS 291
#define SOF_CS_EFFECTS 647
#define SOF_CS_IMAGES 903
#define SOF_CS_LIGHTS 1159
#define SOF_CS_PLAYERSKINS 1415
#define SOF_CS_WELCOME 1493
#define SOF_CS_GHOULFILES 1497

/* entity header bits (SPEC-202 2.1) */
#define SOF_U_ORIGIN1 (1u << 0)
#define SOF_U_ORIGIN2 (1u << 1)
#define SOF_U_ANGLE2 (1u << 2)
#define SOF_U_ANGLE3 (1u << 3)
#define SOF_U_ANGLE1 (1u << 4)
#define SOF_U_ORIGIN3 (1u << 5)
#define SOF_U_EVENTS (1u << 6)
#define SOF_U_MORE1 (1u << 7)
#define SOF_U_RENDERFX16 (1u << 8)
#define SOF_U_MODEL2 (1u << 9)
#define SOF_U_RENDERFX8 (1u << 10)
#define SOF_U_REMOVE (1u << 11)
#define SOF_U_FRAME16 (1u << 12)
#define SOF_U_EFFECTSLOTS (1u << 13)
#define SOF_U_EFFECTS16 (1u << 14)
#define SOF_U_MORE2 (1u << 15)
#define SOF_U_EFFECTS8 (1u << 16)
#define SOF_U_FRAME8 (1u << 17)
#define SOF_U_SOLID (1u << 18)
#define SOF_U_SKIN8 (1u << 19)
#define SOF_U_MODEL (1u << 20)
#define SOF_U_SOUND (1u << 21)
#define SOF_U_SOUND256 (1u << 22)
#define SOF_U_MORE3 (1u << 23)
#define SOF_U_NUMBER16 (1u << 24)
#define SOF_U_SKIN16 (1u << 25)
#define SOF_U_YAWOFS (1u << 26)

/* effects / renderfx bits used here (SPEC-202 2.4) */
#define SOF_EF_ROTATE 0x2000
#define SOF_RF_NOLERP 0x40
#define SOF_RF_SOUNDONLY 0x80
#define SOF_RF_GHOUL 0x400000

/* player state flags (SPEC-202 section 4) */
#define SOF_PS_M_TYPE 0x00000001
#define SOF_PS_M_ORIGIN 0x00000002
#define SOF_PS_M_VELOCITY 0x00000004
#define SOF_PS_M_TIME 0x00000008
#define SOF_PS_M_FLAGS 0x00000010
#define SOF_PS_M_GRAVITY 0x00000020
#define SOF_PS_M_DELTA_ANGLES 0x00000040
#define SOF_PS_VIEWOFFSET 0x00000080
#define SOF_PS_VIEWANGLES 0x00000100
#define SOF_PS_KICKANGLES 0x00000200
#define SOF_PS_BLEND 0x00000400
#define SOF_PS_FOV 0x00000800
#define SOF_PS_REMOTE_ORIGIN 0x00001000
#define SOF_PS_REMOTE_VIEW 0x00002000
#define SOF_PS_RDFLAGS 0x00004000
#define SOF_PS_REMOTE_ANGLES 0x00008000
#define SOF_PS_GUN 0x00010000
#define SOF_PS_GUN_COUNT_A 0x00020000
#define SOF_PS_GUN_COUNT_B 0x00040000
#define SOF_PS_WEAPON_KICK 0x00080000
#define SOF_PS_REMOTE_GHOUL 0x00100000
#define SOF_PS_CINFREEZE 0x00400000
#define SOF_PS_MUSIC 0x00800000
#define SOF_PS_AMBIENT 0x01000000
#define SOF_PS_RANK 0x02000000
#define SOF_PS_SPECTATED 0x04000000
#define SOF_PS_PMEXTRA 0x08000000
#define SOF_PS_RESYNC 0x10000000
#define SOF_PS_INHIBIT 0x20000000
#define SOF_PS_WEAPONSEQ 0x40000000

#define SOF_NUM_STATS 16

/* user command buttons (SPEC-202 6.1) */
#define SOF_BUTTON_ALTATTACK 0x04
#define SOF_BUTTON_WEAPONEXTRA1 0x08
#define SOF_BUTTON_WEAPONEXTRA2 0x10
#define SOF_BUTTON_RUN 0x20

/* ---------------------------------------------------------------------
 * state
 * --------------------------------------------------------------------- */

cvar_t *cl_protocol;

static qboolean sof_proto;        /* the current connection speaks protocol 33 */
static int sof_nonce;             /* sent in getchallenge and connect (SPEC-200) */
static char sof_cs[SOF_MAX_CONFIGSTRINGS][SOF_CS_LEN];
static float sof_gametime;        /* game time of the last frame (SPEC-202 section 1) */
static int sof_ps_music[UPDATE_BACKUP];   /* player-state music value per frame */
static int sof_ps_ambient[UPDATE_BACKUP]; /* player-state ambient-set value per frame */
static qboolean sof_welcome_shown;

typedef struct
{
	int lean;
	float firetime, altfiretime;
} sof_cmdext_t;

static sof_cmdext_t sof_cmdext[CMD_BACKUP];

qboolean
CL_SoF_Active(void)
{
	return sof_proto;
}

const char *
CL_SoF_ConfigString(int index)
{
	if ((index < 0) || (index >= SOF_MAX_CONFIGSTRINGS))
	{
		return "";
	}

	return sof_cs[index];
}

/* OpenMerc (I1a): player-state audio values of the current frame */
void
CL_SoF_FrameAudio(int *ambientset, int *musicmode)
{
	int slot = cl.frame.serverframe & UPDATE_MASK;

	*ambientset = sof_proto ? sof_ps_ambient[slot] : 0;
	*musicmode = sof_proto ? sof_ps_music[slot] : 0;
}

static void
SoF_CheckRead(const char *what)
{
	if (net_message.readcount > net_message.cursize)
	{
		Com_Error(ERR_DROP, "SoF: message ended inside %s", what);
	}
}

static void
SoF_Skip(int bytes)
{
	if ((bytes < 0) || (net_message.readcount + bytes > net_message.cursize))
	{
		Com_Error(ERR_DROP, "SoF: bad length %d", bytes);
	}

	net_message.readcount += bytes;
}

static int
SoF_ReadUShort(void)
{
	return MSG_ReadShort(&net_message) & 0xFFFF;
}

static unsigned
SoF_ReadULong(void)
{
	return (unsigned)MSG_ReadLong(&net_message);
}

/* SPEC-201 'dir': index 0..249, anything else is fatal */
static void
SoF_ReadDir(void)
{
	int d = MSG_ReadByte(&net_message);

	if ((d < 0) || (d >= SOF_NUMDIRS))
	{
		Com_Error(ERR_DROP, "MSF_ReadDir: out of range");
	}
}

/* OpenMerc (I5): the direction table for the effects system (SPEC-201 'dir') */
void
CL_SoF_DirVector(int index, vec3_t out)
{
	if ((index < 0) || (index >= SOF_NUMDIRS))
	{
		Com_Error(ERR_DROP, "MSF_ReadDir: out of range");
		VectorClear(out);
		return;
	}

	memcpy(out, sof_dir_bytes + 12 * index, 12);
}

int
CL_SoF_DirIndex(const vec3_t v)
{
	int i, best = 0;
	float d, bestd = 0;
	vec3_t t;

	for (i = 0; i < SOF_NUMDIRS; i++)
	{
		memcpy(t, sof_dir_bytes + 12 * i, 12);
		d = DotProduct(v, t);

		if (d > bestd)
		{
			bestd = d;
			best = i;
		}
	}

	return best;
}

/* ---------------------------------------------------------------------
 * connection (SPEC-200, SPEC-203)
 * --------------------------------------------------------------------- */

void
CL_SoF_Init(void)
{
	cl_protocol = Cvar_Get("cl_protocol", "33", CVAR_ARCHIVE);

	/* userinfo keys the 1.07f engine and game read (SPEC-203) */
	Cvar_Get("teamname", "mullins", CVAR_USERINFO | CVAR_ARCHIVE);
	Cvar_Get("bestweap", "safe", CVAR_USERINFO | CVAR_ARCHIVE);
	Cvar_Get("team_red_blue", "0", CVAR_USERINFO | CVAR_ARCHIVE);
	Cvar_Get("spectator_password", "", CVAR_USERINFO);
	Cvar_Get("allow_download_models", "1", CVAR_USERINFO | CVAR_ARCHIVE);
	Cvar_Get("cl_violence", "0", CVAR_USERINFO | CVAR_ARCHIVE);
	/* no client-side weapon prediction yet (net-verification.md item 9) */
	Cvar_Get("predicting", "0", CVAR_USERINFO);
	/* OpenMerc (I1b): the SPEC-302 table registers "1"; the engine client hosts no
	   player-module predictor yet (SPEC-003 client side), and a client that reports
	   predicting 1 is expected to send the predicted fire times - without them the
	   game never fires (OpenMerc client on the original and on the OpenMerc server,
	   single player and multiplayer, 2026-10-25). The original sets it from
	   cl_predict only for servers that are not localhost. */
	Cvar_ForceSet("predicting", "0");
}

/* Decides the protocol of a new connection: protocol 33 when cl_protocol is 33
   (remote servers and, since I1b, the local built-in server too). */
static qboolean
SoF_WantProtocol(void)
{
	/* OpenMerc (I1b): the built-in server speaks only protocol 33 too (SPEC-200..203), so the
	   local connection uses it as well */
	return cl_protocol && ((int)cl_protocol->value == SOF_PROTOCOL_VERSION);
}

qboolean
CL_SoF_SendGetChallenge(netadr_t adr)
{
	if (!SoF_WantProtocol())
	{
		return false;
	}

	/* SPEC-200: the nonce is the time the connection attempt started */
	if (!sof_nonce)
	{
		sof_nonce = (int)time(NULL);
	}

	Netchan_OutOfBandPrint(NS_CLIENT, adr, "getchallenge %i\n", sof_nonce);
	return true;
}

qboolean
CL_SoF_SendConnect(netadr_t adr, int qport, int challenge)
{
	cvar_t *skin;

	if (!SoF_WantProtocol())
	{
		sof_proto = false;
		return false;
	}

	sof_proto = true;

	if (!sof_nonce)
	{
		sof_nonce = (int)time(NULL);
	}

	/* Quake II player skins ("male/grunt") mean nothing to a SoF server */
	skin = Cvar_Get("skin", "mullins", CVAR_USERINFO | CVAR_ARCHIVE);

	if (strchr(skin->string, '/'))
	{
		Cvar_Set("skin", "mullins");
	}

	userinfo_modified = false;

	/* SPEC-200: connect 33 <qport> <challenge> <nonce> 3.14 "<userinfo>" */
	Netchan_OutOfBandPrint(NS_CLIENT, adr, "connect %i %i %i %i 3.14 \"%s\"\n",
			SOF_PROTOCOL_VERSION, qport, challenge, sof_nonce, Cvar_Userinfo());

	return true;
}

/* A new connection attempt starts (connect command, reconnect) */
void
CL_SoF_NewAttempt(void)
{
	sof_nonce = 0;
}

/* The connection is gone; the next one decides its protocol again */
void
CL_SoF_Reset(void)
{
	sof_proto = false;
	sof_nonce = 0;
}

/* OpenMerc (I6h): demo playback parses protocol 33 without a connection */
void
CL_SoF_SetDemoProtocol(qboolean on)
{
	sof_proto = on;
}

void MS_ServerAck(netadr_t adr, int sent); /* OpenMerc (I6m): menu server browser */

/* SPEC-200 out-of-band replies that Quake II does not know */
qboolean
CL_SoF_ConnectionlessPacket(const char *c)
{
	static const struct
	{
		const char *cmd;
		const char *text;
	} rejections[] = {
		{"rejected_violence", "server violence settings are incompatible"},
		{"rejected_full", "server is full"},
		{"rejected_error", "rejected"},
		{"rejected_banned", "banned"},
		{"rejected_spectator_password", "bad spectator password"},
		{"rejected_spectator_limit_exeeded", "spectator limit reached"},
		{"rejected_password", "bad password"},
	};
	int i;

	for (i = 0; i < sizeof(rejections) / sizeof(rejections[0]); i++)
	{
		if (!strcmp(c, rejections[i].cmd))
		{
			if (cls.state == ca_connecting)
			{
				Com_Printf("Connection refused: %s\n", rejections[i].text);
				cls.state = ca_disconnected;
				cls.connect_time = 0;
			}

			return true;
		}
	}

	if (!strcmp(c, "ack"))
	{
		MS_ServerAck(net_from, atoi(Cmd_Argv(1))); /* ping reply: browser ping (I6m) */
		return true;
	}

	if (!strcmp(c, "rejected_auth"))
	{
		return true; /* never sent by 1.07f servers */
	}

	return false;
}

/* ---------------------------------------------------------------------
 * configstrings (SPEC-203)
 * --------------------------------------------------------------------- */

/* Copies a SoF configstring into the Quake II slot the rest of the client
   reads, for the parts that have a Quake II counterpart. */
static void
SoF_MapConfigString(int i)
{
	int q2 = -1, n;
	const char *s = sof_cs[i];

	/* OpenMerc (I5): effect definitions (SPEC-400 Effect tables) */
	if ((i > SOF_CS_EFFECTS) && (i < SOF_CS_EFFECTS + 256))
	{
		CLFX_EffectConfigString(i - SOF_CS_EFFECTS, s);
	}

	if (i <= 4)
	{
		q2 = i; /* name, cd track, sky, sky axis, sky rotate */
	}
	else if (i == SOF_CS_MAXCLIENTS)
	{
		q2 = CS_MAXCLIENTS;
	}
	else if ((i > SOF_CS_MODELS) && (i < SOF_CS_MODELS + MAX_MODELS))
	{
		n = i - SOF_CS_MODELS;

		/* only the world and its inline models; Ghoul models have no
		   Quake II renderer counterpart (placeholders are drawn) */
		if ((s[0] == '*') || !strncmp(s, "maps/", 5) || !s[0])
		{
			q2 = CS_MODELS + n;
		}
	}
	else if ((i >= SOF_CS_LIGHTS) && (i < SOF_CS_LIGHTS + MAX_LIGHTSTYLES))
	{
		q2 = CS_LIGHTS + (i - SOF_CS_LIGHTS);
	}

	if (q2 < 0)
	{
		return;
	}

	Q_strlcpy(cl.configstrings[q2], s, sizeof(cl.configstrings[q2]));

	if ((q2 >= CS_LIGHTS) && (q2 < CS_LIGHTS + MAX_LIGHTSTYLES))
	{
		CL_SetLightstyle(q2 - CS_LIGHTS);
	}
	else if ((q2 >= CS_MODELS) && (q2 < CS_MODELS + MAX_MODELS) && cl.refresh_prepped && s[0])
	{
		cl.model_draw[q2 - CS_MODELS] = R_RegisterModel(cl.configstrings[q2]);
		cl.model_clip[q2 - CS_MODELS] = (s[0] == '*') ? CM_InlineModel(cl.configstrings[q2]) : NULL;
	}
}

static void
SoF_ParseConfigString(void)
{
	int i, code;
	const char *s;

	i = MSG_ReadShort(&net_message);
	code = MSG_ReadShort(&net_message);

	if (code < 0)
	{
		s = MSG_ReadString(&net_message);
	}
	else
	{
		/* no dictionary (docs/DEVIATIONS.md "Fixed tables"): a server that sends codes is not ours */
		Com_Error(ERR_DROP, "SoF: configstring dictionary code %d (OpenMerc has no dictionary)", code);
		return;
	}

	SoF_CheckRead("svc_configstring");

	if ((i < 0) || (i >= SOF_MAX_CONFIGSTRINGS))
	{
		Com_Error(ERR_DROP, "configstring > MAX_CONFIGSTRINGS");
		return;
	}

	Q_strlcpy(sof_cs[i], s, SOF_CS_LEN);
	SoF_MapConfigString(i);
	SNDH_ConfigStringChanged(i, sof_cs[i], false); /* OpenMerc (I1a) */

	/* OpenMerc (I6h, SPEC-AMEND-005 I6h.5): every configstring received while the level's graphics are not
	   registered is counted by one process-wide counter (never reset); 8 of every 16 (counter mod 16 = 0...7 before
	   counting) with an index below 1415 set menu_loading = 0.33 + 0.33 x index / 1415 and run a complete screen
	   update */
	if (!cl.refresh_prepped)
	{
		static unsigned int loading_counter;
		unsigned int before = loading_counter++;

		if (((before % 16) < 8) && (i < 1415))
		{
			SCR_SoF_LoadingProgress(0.33f + 0.33f * (float)i / 1415.0f);
		}
	}
}

static void
SoF_RemoveConfigString(void)
{
	int i = MSG_ReadShort(&net_message);

	if ((i < 0) || (i >= SOF_MAX_CONFIGSTRINGS))
	{
		Com_Error(ERR_DROP, "SoF: removeconfigstring %d", i);
		return;
	}

	sof_cs[i][0] = 0;
	SoF_MapConfigString(i);
	SNDH_ConfigStringChanged(i, "", true); /* OpenMerc (I1a) */
}

static void
SoF_ParseServerData(void)
{
	extern cvar_t *fs_gamedirvar;
	char *str;
	int i;

	In_FlushQueue();

	Com_DPrintf("Serverdata packet received (protocol 33).\n");

	CL_ClearState();
	memset(sof_cs, 0, sizeof(sof_cs));
	sof_welcome_shown = false;
	cls.state = ca_connected;

	i = MSG_ReadLong(&net_message);
	cls.serverProtocol = i;

	if (i != SOF_PROTOCOL_VERSION)
	{
		Com_Error(ERR_DROP, "Server returned version %i, not %i", i, SOF_PROTOCOL_VERSION);
		return;
	}

	cl.servercount = MSG_ReadLong(&net_message);
	cl.attractloop = MSG_ReadByte(&net_message);
	CL_SoF_SetServerDeathmatch(MSG_ReadByte(&net_message)); /* OpenMerc (I6h) */
	CL_Demo_ClearLevel(); /* OpenMerc (I6h) */

	str = MSG_ReadString(&net_message);
	Q_strlcpy(cl.gamedir, str, sizeof(cl.gamedir));

	if ((*str && (!fs_gamedirvar->string || !*fs_gamedirvar->string ||
		  strcmp(fs_gamedirvar->string, str))) ||
		(!*str && (fs_gamedirvar->string && *fs_gamedirvar->string)))
	{
		Cvar_Set("game", str);
	}

	cl.playernum = MSG_ReadShort(&net_message);
	str = MSG_ReadString(&net_message);
	SoF_CheckRead("svc_serverdata");

	if (cl.playernum != -1)
	{
		Com_Printf("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");
		Com_Printf("%c%s\n", 2, str);
	}

	cl.refresh_prepped = false;
}

/* ---------------------------------------------------------------------
 * map check string (SPEC-203)
 * --------------------------------------------------------------------- */

static void
SoF_CheckMapString(void)
{
	byte *buf;
	int len, i, ofs, l;
	char mine[64];

	len = FS_LoadFile(cl.configstrings[CS_MODELS + 1], (void **)&buf);

	if (!buf || (len < 8 + 22 * 8))
	{
		if (buf)
		{
			FS_FreeFile(buf);
		}

		return;
	}

	mine[0] = 0;

	for (i = 0; i < 22; i++)
	{
		ofs = LittleLong(*(int *)(buf + 8 + 8 * i));
		l = LittleLong(*(int *)(buf + 12 + 8 * i));

		if ((ofs < 0) || (l < 0) || (ofs + l > len))
		{
			break;
		}

		Q_strlcat(mine, va("%02X", Com_BlockChecksum(buf + ofs, l) & 0xFF), sizeof(mine));
	}

	FS_FreeFile(buf);

	if (strcmp(mine, sof_cs[SOF_CS_MAPCHECK]))
	{
		Com_Printf("WARNING: local map differs from the server's (%s != %s)\n",
				mine, sof_cs[SOF_CS_MAPCHECK]);
	}
	else
	{
		Com_DPrintf("Map checkstring: %s\n", mine);
	}
}

/* Console command "precache <sc>" from the server (SPEC-203 section 3) */
void
CL_SoF_Precache(int spawncount)
{
	unsigned checksum;
	int i;
	const char *name;

	CM_LoadMap(cl.configstrings[CS_MODELS + 1], true, &checksum);
	SoF_CheckMapString();
	SNDH_RegisterLevel(); /* OpenMerc (I1a): SoF sound registration */
	CL_PrepRefresh();

	/* inline models past a gap in the model list */
	for (i = 1; i < MAX_MODELS; i++)
	{
		name = cl.configstrings[CS_MODELS + i];

		if ((name[0] == '*') && !cl.model_draw[i])
		{
			cl.model_draw[i] = R_RegisterModel(name);
			cl.model_clip[i] = CM_InlineModel(name);
		}
	}

	CL_Ghoul_LoadingDone(); /* I1c: loading finished - system methods 23, 7 (SPEC-010) */

	/* the 1.07f client executes "sv_precache <sc>", which is forwarded */
	MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
	MSG_WriteString(&cls.netchan.message, va("sv_precache %i", spawncount));
}

/* ---------------------------------------------------------------------
 * entities (SPEC-202 sections 2 and 3)
 * --------------------------------------------------------------------- */

static int
SoF_ParseEntityBits(unsigned *bits)
{
	unsigned b, total;
	int number;

	total = MSG_ReadByte(&net_message);

	if (total & SOF_U_MORE1)
	{
		b = MSG_ReadByte(&net_message);
		total |= b << 8;
	}

	if (total & SOF_U_MORE2)
	{
		b = MSG_ReadByte(&net_message);
		total |= b << 16;
	}

	if (total & SOF_U_MORE3)
	{
		b = MSG_ReadByte(&net_message);
		total |= b << 24;
	}

	if (total & SOF_U_NUMBER16)
	{
		number = MSG_ReadShort(&net_message);
	}
	else
	{
		number = MSG_ReadByte(&net_message);
	}

	*bits = total;
	return number;
}

static void
SoF_ParseDelta(const entity_state_t *from, entity_state_t *to, int number, unsigned bits)
{
	entity_state_t dummy;
	int i;

	if (!to)
	{
		to = &dummy; /* parse but discard */
	}

	if (from)
	{
		*to = *from;
		VectorCopy(from->origin, to->old_origin);
	}
	else
	{
		memset(to, 0, sizeof(*to));
	}

	to->number = number;
	to->event = 0; /* SoF entity events have no Quake II meaning */
	CLFX_DeltaBegin(from, to); /* OpenMerc (I5): events and effect slots */

	if (bits & SOF_U_MODEL)
	{
		to->modelindex = MSG_ReadByte(&net_message);
	}

	if (bits & SOF_U_MODEL2)
	{
		to->modelindex2 = MSG_ReadByte(&net_message);
	}

	if (bits & SOF_U_FRAME8)
	{
		to->frame = MSG_ReadByte(&net_message);
	}
	else if (bits & SOF_U_FRAME16)
	{
		to->frame = MSG_ReadShort(&net_message);
	}

	if ((bits & SOF_U_SKIN8) && (bits & SOF_U_SKIN16))
	{
		to->skinnum = MSG_ReadLong(&net_message);
	}
	else if (bits & SOF_U_SKIN8)
	{
		to->skinnum = MSG_ReadByte(&net_message);
	}
	else if (bits & SOF_U_SKIN16)
	{
		to->skinnum = SoF_ReadUShort();
	}

	if ((bits & SOF_U_EFFECTS8) && (bits & SOF_U_EFFECTS16))
	{
		to->effects = SoF_ReadULong();
	}
	else if (bits & SOF_U_EFFECTS8)
	{
		to->effects = MSG_ReadByte(&net_message);
	}
	else if (bits & SOF_U_EFFECTS16)
	{
		to->effects = SoF_ReadUShort();
	}

	if ((bits & SOF_U_RENDERFX8) && (bits & SOF_U_RENDERFX16))
	{
		to->renderfx = MSG_ReadLong(&net_message);
	}
	else if (bits & SOF_U_RENDERFX8)
	{
		to->renderfx = MSG_ReadByte(&net_message);
	}
	else if (bits & SOF_U_RENDERFX16)
	{
		to->renderfx = SoF_ReadUShort();
	}

	if (bits & SOF_U_ORIGIN1)
	{
		to->origin[0] = MSG_ReadCoord(&net_message);
	}

	if (bits & SOF_U_ORIGIN2)
	{
		to->origin[1] = MSG_ReadCoord(&net_message);
	}

	if (bits & SOF_U_ORIGIN3)
	{
		to->origin[2] = MSG_ReadCoord(&net_message);
	}

	if (bits & SOF_U_ANGLE1)
	{
		to->angles[0] = MSG_ReadAngle(&net_message);
	}

	if (bits & SOF_U_ANGLE2)
	{
		to->angles[1] = MSG_ReadAngle(&net_message);
	}

	if (bits & SOF_U_YAWOFS)
	{
		to->modelindex3 = MSG_ReadByte(&net_message); /* yaw offset, kept raw */
	}

	if (bits & SOF_U_ANGLE3)
	{
		to->angles[2] = MSG_ReadAngle(&net_message);
	}

	if (bits & SOF_U_SOUND)
	{
		to->sound = MSG_ReadByte(&net_message);

		if (bits & SOF_U_SOUND256)
		{
			to->sound += 256;
		}

		to->modelindex4 = MSG_ReadByte(&net_message); /* sound data, kept raw */
	}

	if (bits & SOF_U_EVENTS)
	{
		/* up to three (event, parameter) pairs; bit 7 = another follows */
		for (i = 0; i < 3; i++)
		{
			int ev = MSG_ReadByte(&net_message);

			CLFX_DeltaEvent(to, i, ev, MSG_ReadByte(&net_message)); /* OpenMerc (I5) */

			if (!(ev & 0x80))
			{
				break;
			}
		}
	}

	if (bits & SOF_U_SOLID)
	{
		to->solid = MSG_ReadLong(&net_message);
	}

	if (bits & SOF_U_EFFECTSLOTS)
	{
		int mask = SoF_ReadUShort();

		for (i = 0; i < 12; i++)
		{
			if (mask & (1 << i))
			{
				CLFX_DeltaSlot(to, i, MSG_ReadShort(&net_message)); /* OpenMerc (I5) */
			}
		}
	}
}

/* SPEC-202 2.3: whole-unit bounding box packed into 32 bits */
void
CL_SoF_SolidBox(int solid, vec3_t mins, vec3_t maxs)
{
	unsigned s = (unsigned)solid;

	mins[0] = -(float)(s & 0xFF);
	mins[1] = -(float)((s >> 8) & 0xFF);
	mins[2] = -(float)((s >> 16) & 0xFF);
	maxs[0] = (float)(s & 0xFF);
	maxs[1] = (float)((s >> 8) & 0xFF);
	maxs[2] = (float)((s >> 24) & 0xFF) - 128.0f;
}

static void
SoF_DeltaEntity(frame_t *frame, int newnum, const entity_state_t *old, unsigned bits)
{
	centity_t dummy, *ent;
	entity_state_t *state;

	ent = CL_AllocEntity(newnum);

	if (!ent)
	{
		memset(&dummy, 0, sizeof(dummy));
		ent = &dummy;
	}

	state = &cl_parse_entities[cl.parse_entities & (MAX_PARSE_ENTITIES - 1)];
	cl.parse_entities++;
	frame->num_entities++;

	SoF_ParseDelta(old, state, newnum, bits);

	/* SPEC-202 3: these changes reset interpolation */
	if ((state->modelindex != ent->current.modelindex) ||
		(state->modelindex2 != ent->current.modelindex2) ||
		(fabs(state->origin[0] - ent->current.origin[0]) > 512) ||
		(fabs(state->origin[1] - ent->current.origin[1]) > 512) ||
		(fabs(state->origin[2] - ent->current.origin[2]) > 512))
	{
		ent->serverframe = -99;
	}

	if (ent->serverframe != cl.frame.serverframe - 1)
	{
		/* wasn't in the last update */
		ent->trailcount = 1024;
		ent->prev = *state;
		ent->serverframe_created = cl.frame.serverframe;
		VectorCopy(state->origin, ent->prev.origin);
		VectorCopy(state->origin, ent->lerp_origin);
	}
	else
	{
		ent->prev = ent->current;
	}

	ent->serverframe = cl.frame.serverframe;
	ent->current = *state;
}

static void
SoF_NextOld(frame_t *oldframe, int *oldindex, entity_state_t **oldstate, int *oldnum)
{
	(*oldindex)++;

	if (!oldframe || (*oldindex >= oldframe->num_entities))
	{
		*oldnum = 99999;
	}
	else
	{
		*oldstate = &cl_parse_entities[(oldframe->parse_entities + *oldindex) &
			(MAX_PARSE_ENTITIES - 1)];
		*oldnum = (*oldstate)->number;
	}
}

static void
SoF_ParsePacketEntities(frame_t *oldframe, frame_t *newframe)
{
	int newnum, oldindex, oldnum;
	unsigned bits;
	entity_state_t *oldstate = NULL;
	centity_t *ent;

	newframe->parse_entities = cl.parse_entities;
	newframe->num_entities = 0;

	oldindex = -1;
	SoF_NextOld(oldframe, &oldindex, &oldstate, &oldnum);

	while (1)
	{
		newnum = SoF_ParseEntityBits(&bits);

		if ((newnum < 0) || (newnum >= MAX_EDICTS))
		{
			Com_Error(ERR_DROP, "CL_ParsePacketEntities: bad number:%i", newnum);
			return;
		}

		if (net_message.readcount > net_message.cursize)
		{
			Com_Error(ERR_DROP, "CL_ParsePacketEntities: end of message");
			return;
		}

		if (!newnum && !bits)
		{
			break; /* list terminator 00 00 */
		}

		while (oldnum < newnum)
		{
			/* unchanged from the reference frame */
			SoF_DeltaEntity(newframe, oldnum, oldstate, 0);
			SoF_NextOld(oldframe, &oldindex, &oldstate, &oldnum);
		}

		if (bits & SOF_U_REMOVE)
		{
			if (oldnum != newnum)
			{
				Com_Printf("U_REMOVE: oldnum != newnum\n");
			}
			else
			{
				SoF_NextOld(oldframe, &oldindex, &oldstate, &oldnum);
			}

			continue;
		}

		if (oldnum == newnum)
		{
			SoF_DeltaEntity(newframe, newnum, oldstate, bits);
			SoF_NextOld(oldframe, &oldindex, &oldstate, &oldnum);
			continue;
		}

		/* oldnum > newnum: new entity, delta from its baseline */
		ent = CL_AllocEntity(newnum);
		SoF_DeltaEntity(newframe, newnum, ent ? &ent->baseline : NULL, bits);
	}

	while (oldnum != 99999)
	{
		SoF_DeltaEntity(newframe, oldnum, oldstate, 0);
		SoF_NextOld(oldframe, &oldindex, &oldstate, &oldnum);
	}
}

static void
SoF_ParseBaseline(void)
{
	unsigned bits;
	int newnum;
	centity_t *ent;
	int start = net_message.readcount; /* OpenMerc (I6h): raw bytes for demos */

	newnum = SoF_ParseEntityBits(&bits);

	if ((newnum < 0) || (newnum >= MAX_EDICTS))
	{
		Com_Error(ERR_DROP, "SoF: baseline number %i", newnum);
		return;
	}

	ent = CL_AllocEntity(newnum);
	SoF_ParseDelta(NULL, ent ? &ent->baseline : NULL, newnum, bits);
	SoF_CheckRead("svc_spawnbaseline");

	if (ent && (ent->baseline.modelindex || (ent->baseline.renderfx & SOF_RF_GHOUL)))
	{
		CL_Demo_StoreBaseline(newnum, net_message.data + start, net_message.readcount - start);
	}
}

/* ---------------------------------------------------------------------
 * player state (SPEC-202 section 4)
 * --------------------------------------------------------------------- */

static void
SoF_ParsePlayerstate(frame_t *oldframe, frame_t *newframe)
{
	unsigned flags, statbits;
	int i;
	player_state_t *state = &newframe->playerstate;
	sofps_t *x, *ox; /* OpenMerc (I6h) */

	if (oldframe)
	{
		*state = oldframe->playerstate;
	}
	else
	{
		memset(state, 0, sizeof(*state));
	}

	flags = SoF_ReadULong();

	/* OpenMerc (I6h): the fields without a Quake II counterpart are kept in
	   the frame's sofps_t record (cl_view.c) */
	x = CL_SoF_PlayerExt(newframe->serverframe);
	ox = oldframe ? CL_SoF_PlayerExt(oldframe->serverframe) : NULL;

	if (ox && (ox != x) && ox->valid && (ox->serverframe == oldframe->serverframe))
	{
		*x = *ox;
	}
	else if (ox != x)
	{
		memset(x, 0, sizeof(*x));
	}

	x->valid = 1;
	x->serverframe = newframe->serverframe;

	if (flags & SOF_PS_GUN)
	{
		x->gun_id = SoF_ReadUShort();
		x->gun_type = MSG_ReadByte(&net_message);
	}

	if (flags & SOF_PS_GUN_COUNT_A)
	{
		x->gun_count_a = MSG_ReadByte(&net_message);
	}

	if (flags & SOF_PS_GUN_COUNT_B)
	{
		x->gun_count_b = MSG_ReadShort(&net_message);
	}

	if (flags & SOF_PS_WEAPONSEQ)
	{
		x->weapon_seq = MSG_ReadByte(&net_message);
	}

	if (flags & SOF_PS_RESYNC)
	{
		x->resync = MSG_ReadByte(&net_message);
	}

	if (flags & SOF_PS_INHIBIT)
	{
		x->inhibit = MSG_ReadByte(&net_message);
	}

	if (flags & SOF_PS_REMOTE_GHOUL)
	{
		x->remote_ghoul = SoF_ReadUShort();
	}

	if (flags & SOF_PS_M_TYPE)
	{
		/* SoF pm_type -> Quake II pm_type */
		static const int pmtype[8] = {
			PM_NORMAL, PM_SPECTATOR, PM_SPECTATOR, PM_DEAD,
			PM_GIB, PM_FREEZE, PM_NORMAL, PM_FREEZE
		};
		i = MSG_ReadByte(&net_message);
		x->pm_type = i;
		state->pmove.pm_type = ((i >= 0) && (i < 8)) ? pmtype[i] : PM_FREEZE;
	}

	if (flags & SOF_PS_M_ORIGIN)
	{
		state->pmove.origin[0] = MSG_ReadShort(&net_message);
		state->pmove.origin[1] = MSG_ReadShort(&net_message);
		state->pmove.origin[2] = MSG_ReadShort(&net_message);
	}

	if (flags & SOF_PS_M_VELOCITY)
	{
		state->pmove.velocity[0] = MSG_ReadShort(&net_message);
		state->pmove.velocity[1] = MSG_ReadShort(&net_message);
		state->pmove.velocity[2] = MSG_ReadShort(&net_message);
	}

	if (flags & SOF_PS_M_TIME)
	{
		state->pmove.pm_time = MSG_ReadByte(&net_message);
	}

	if (flags & SOF_PS_M_FLAGS)
	{
		/* the captures show the Quake II meaning of bits 1, 2, 4 and 16 */
		state->pmove.pm_flags = MSG_ReadByte(&net_message);
	}

	if (flags & SOF_PS_M_GRAVITY)
	{
		state->pmove.gravity = MSG_ReadShort(&net_message);
	}

	if (flags & SOF_PS_M_DELTA_ANGLES)
	{
		state->pmove.delta_angles[0] = MSG_ReadShort(&net_message);
		state->pmove.delta_angles[1] = MSG_ReadShort(&net_message);
		state->pmove.delta_angles[2] = MSG_ReadShort(&net_message);
	}

	if (flags & SOF_PS_PMEXTRA)
	{
		x->pm_extra = MSG_ReadByte(&net_message);
	}

	if (cl.attractloop)
	{
		state->pmove.pm_type = PM_FREEZE;
	}

	if (flags & SOF_PS_VIEWOFFSET)
	{
		state->viewoffset[0] = MSG_ReadChar(&net_message) * 0.25f;
		state->viewoffset[1] = MSG_ReadChar(&net_message) * 0.25f;
		state->viewoffset[2] = MSG_ReadChar(&net_message) * 0.2f + 14.6f;
	}

	if (flags & SOF_PS_VIEWANGLES)
	{
		state->viewangles[0] = MSG_ReadAngle16(&net_message);
		state->viewangles[1] = MSG_ReadAngle16(&net_message);
		state->viewangles[2] = MSG_ReadAngle16(&net_message);
	}

	if (flags & SOF_PS_REMOTE_ANGLES)
	{
		for (i = 0; i < 3; i++)
		{
			x->remote_angles[i] = MSG_ReadShort(&net_message);
		}
	}

	if (flags & SOF_PS_REMOTE_ORIGIN)
	{
		for (i = 0; i < 3; i++)
		{
			x->remote_origin[i] = MSG_ReadShort(&net_message);
		}
	}

	if (flags & SOF_PS_REMOTE_VIEW)
	{
		x->remote_type = MSG_ReadByte(&net_message);
		x->remote_ent = MSG_ReadByte(&net_message);
	}

	if (flags & SOF_PS_KICKANGLES)
	{
		state->kick_angles[0] = MSG_ReadChar(&net_message) * 0.25f;
		state->kick_angles[1] = MSG_ReadChar(&net_message) * 0.25f;
		state->kick_angles[2] = MSG_ReadChar(&net_message) * 0.25f;
	}

	if (flags & SOF_PS_WEAPON_KICK)
	{
		x->weapon_kick = MSG_ReadChar(&net_message) * 0.25f;
	}

	if (flags & SOF_PS_BLEND)
	{
		state->blend[0] = MSG_ReadByte(&net_message) / 255.0f;
		state->blend[1] = MSG_ReadByte(&net_message) / 255.0f;
		state->blend[2] = MSG_ReadByte(&net_message) / 255.0f;
		state->blend[3] = MSG_ReadByte(&net_message) / 255.0f;
	}

	if (flags & SOF_PS_FOV)
	{
		state->fov = (float)MSG_ReadByte(&net_message);
		x->fov = (int)state->fov;
	}

	if (flags & SOF_PS_RDFLAGS)
	{
		/* only "underwater" is known to match Quake II */
		x->rdflags = MSG_ReadByte(&net_message);
		state->rdflags = x->rdflags & RDF_UNDERWATER;
	}

	statbits = SoF_ReadULong();

	for (i = 0; i < 32; i++)
	{
		if (statbits & (1u << i))
		{
			if (i >= SOF_NUM_STATS)
			{
				Com_Error(ERR_DROP, "SoF: stat %d", i);
				return;
			}

			state->stats[i] = MSG_ReadShort(&net_message);
		}
	}

	for (i = 0; i < SOF_NUM_STATS; i++)
	{
		x->stats[i] = state->stats[i];
	}

	if (flags & SOF_PS_CINFREEZE)
	{
		x->cinfreeze = MSG_ReadByte(&net_message);
	}

	/* OpenMerc (I1a): music mode and general ambient set feed the sound
	   module (SPEC-004 sound struct #1 +0x9AA / +0x9A8) */
	{
		int slot = newframe->serverframe & UPDATE_MASK;
		int oldslot = oldframe ? (oldframe->serverframe & UPDATE_MASK) : -1;

		sof_ps_music[slot] = (oldslot >= 0) ? sof_ps_music[oldslot] : 0;
		sof_ps_ambient[slot] = (oldslot >= 0) ? sof_ps_ambient[oldslot] : 0;

		if (flags & SOF_PS_MUSIC)
		{
			sof_ps_music[slot] = MSG_ReadByte(&net_message);
		}

		if (flags & SOF_PS_AMBIENT)
		{
			sof_ps_ambient[slot] = MSG_ReadShort(&net_message);
		}
	}

	if (flags & SOF_PS_RANK)
	{
		x->rank = MSG_ReadByte(&net_message);
		x->rankcount = MSG_ReadByte(&net_message);
	}

	if (flags & SOF_PS_SPECTATED)
	{
		x->spectated = MSG_ReadByte(&net_message);
	}

	x->music = sof_ps_music[newframe->serverframe & UPDATE_MASK];
	x->ambient = sof_ps_ambient[newframe->serverframe & UPDATE_MASK];
	CL_SoF_PlayerExtParsed(x, flags); /* OpenMerc (I6h): near clip, freeze cvar */
}

/* ---------------------------------------------------------------------
 * frame (SPEC-202 sections 1 and 5)
 * --------------------------------------------------------------------- */

static void
SoF_ParseFrame(void)
{
	int cmd, len;
	frame_t *old;

	memset(&cl.frame, 0, sizeof(cl.frame));

	cl.frame.serverframe = MSG_ReadLong(&net_message);
	cl.frame.deltaframe = MSG_ReadLong(&net_message);
	cl.frame.servertime = cl.frame.serverframe * 100;
	sof_gametime = MSG_ReadFloat(&net_message);
	CL_Ghoul_FrameParsed(cl.frame.serverframe, sof_gametime); /* I1c: svc 27 time base, game clock */

	if (cl_shownet->value == 3)
	{
		Com_Printf("   frame:%i  delta:%i  time:%.1f\n", cl.frame.serverframe,
				cl.frame.deltaframe, sof_gametime);
	}

	if (cl.frame.deltaframe <= 0)
	{
		cl.frame.valid = true;
		old = NULL;
		CL_Demo_UncompressedFrame(); /* OpenMerc (I6h): ghoul state, recording starts */
	}
	else
	{
		old = &cl.frames[cl.frame.deltaframe & UPDATE_MASK];

		if (!old->valid)
		{
			Com_Printf("Delta from invalid frame (not supposed to happen!).\n");
		}

		if (old->serverframe != cl.frame.deltaframe)
		{
			Com_Printf("Delta frame too old.\n");
		}
		else if (cl.parse_entities - old->parse_entities > MAX_PARSE_ENTITIES - 128)
		{
			Com_Printf("Delta parse_entities too old.\n");
		}
		else
		{
			cl.frame.valid = true;
		}
	}

	if (cl.time > cl.frame.servertime)
	{
		cl.time = cl.frame.servertime;
	}
	else if (cl.time < cl.frame.servertime - 100)
	{
		cl.time = cl.frame.servertime - 100;
	}

	len = MSG_ReadByte(&net_message);

	if ((len < 0) || (len > (int)sizeof(cl.frame.areabits)))
	{
		Com_Error(ERR_DROP, "SoF: areabits length %d", len);
		return;
	}

	MSG_ReadData(&net_message, &cl.frame.areabits, len);

	cmd = MSG_ReadByte(&net_message);

	if (cmd != sofsvc_playerinfo)
	{
		Com_Error(ERR_DROP, "CL_ParseFrame: not playerinfo");
		return;
	}

	if (cl_shownet->value >= 2)
	{
		Com_Printf("%3i:%s\n", net_message.readcount - 1, "svc_playerinfo");
	}

	SoF_ParsePlayerstate(old, &cl.frame);

	cmd = MSG_ReadByte(&net_message);

	if (cmd != sofsvc_packetentities)
	{
		Com_Error(ERR_DROP, "CL_ParseFrame: not packetentities");
		return;
	}

	if (cl_shownet->value >= 2)
	{
		Com_Printf("%3i:%s\n", net_message.readcount - 1, "svc_packetentities");
	}

	SoF_ParsePacketEntities(old, &cl.frame);
	SoF_CheckRead("svc_frame");

	cl.frames[cl.frame.serverframe & UPDATE_MASK] = cl.frame;

	if (cl.frame.valid)
	{
		if (cls.state != ca_active)
		{
			cls.state = ca_active;
			cl.force_refdef = true;
			cl.predicted_origin[0] = cl.frame.playerstate.pmove.origin[0] * 0.125f;
			cl.predicted_origin[1] = cl.frame.playerstate.pmove.origin[1] * 0.125f;
			cl.predicted_origin[2] = cl.frame.playerstate.pmove.origin[2] * 0.125f;
			VectorCopy(cl.frame.playerstate.viewangles, cl.predicted_angles);

			if ((cls.disable_servercount != cl.servercount) && cl.refresh_prepped)
			{
				SCR_EndLoadingPlaque();
			}

			cl.sound_prepped = true;

			if (paused_at_load)
			{
				if (cl_loadpaused->value == 1)
				{
					Cvar_Set("paused", "0");
				}

				paused_at_load = false;
			}
		}

		if (cl_predict->value && !(cl.frame.playerstate.pmove.pm_flags & 0x80)) /* ENG2: SoF flag */
		{
			CL_CheckPredictionError();
		}

		CLFX_FrameParsed(); /* OpenMerc (I5): entity events (SPEC-401) */
	}
}

/* ---------------------------------------------------------------------
 * messages that are parsed and dropped (no consumer yet)
 * --------------------------------------------------------------------- */

/* SPEC-201 section 1 payloads: p pos, d dir, b byte, s short, l long,
   I impact, N byte count + count x (short, byte) */
static const char *sof_te_format[65] = {
	"pd", "p", "pbbbb", "I", "pl", "", "Ipbbb", "Ipbbb", "pbd", "bppbb",
	"pd", "psb", "p", "I", "pd", "pl", "", "pl", "pdbbbbbbb", "I",
	"p", "p", "", "pb", "ppN", "pp", "pb", "ppll", "pl", "I",
	"I", "pd", "pdb", "pb", "pp", "sssb", "sssss", "ss", "p", "s",
	"pb", "sssb", "sss", "pp", "pb", "pbbbb", "pbb", "ps", "ps", "pd",
	"ppss", "p", "ps", "pl", "p", "pbb", "pbb", "pp", "p", "p",
	"bbbpp", "bbbpp", "bbbpp", "bbpp", "ppbbb"
};

static void
SoF_SkipFormat(const char *f)
{
	int n, kind;

	for ( ; *f; f++)
	{
		switch (*f)
		{
			case 'p': SoF_Skip(6); break;
			case 'd': SoF_ReadDir(); break;
			case 'b': SoF_Skip(1); break;
			case 's': SoF_Skip(2); break;
			case 'l': SoF_Skip(4); break;
			case 'N':
				n = MSG_ReadByte(&net_message);
				SoF_Skip(3 * n);
				break;
			case 'I':
				kind = MSG_ReadByte(&net_message);

				switch (kind)
				{
					case 0: SoF_Skip(12); break;
					case 1: SoF_Skip(6); break;
					case 2: SoF_Skip(15); break;
					case 3: SoF_Skip(13); break;
					default:
						Com_Error(ERR_DROP, "SoF: impact kind %d", kind);
						return;
				}

				break;
		}
	}
}

static void
SoF_ParseTempEntity(void)
{
	int type = MSG_ReadByte(&net_message);

	if ((type < 0) || (type > 64))
	{
		Com_Error(ERR_DROP, "SoF: temp entity type %d", type);
		return;
	}

	if (CLFX_Wanted())
	{
		CLFX_ParseTempEntity(type); /* OpenMerc (I5): SPEC-401 */
		return;
	}

	SoF_SkipFormat(sof_te_format[type]);
}

static void
SoF_ParseEffect(void)
{
	int flags, ext;

	if (CLFX_Wanted())
	{
		CLFX_ParseEffect(); /* OpenMerc (I5): SPEC-400 */
		return;
	}

	(void)MSG_ReadByte(&net_message); /* effect configstring slot */
	flags = MSG_ReadByte(&net_message);

	if (flags & 0x01)
	{
		SoF_Skip(6);
	}
	else if (flags & 0x02)
	{
		SoF_Skip(2);
	}
	else if (flags & 0x04)
	{
		SoF_Skip(2);

		if (flags & 0x08)
		{
			SoF_Skip(2);
		}

		SoF_Skip(2);
	}

	if (flags & 0x80)
	{
		ext = MSG_ReadByte(&net_message);

		if (ext & 0x01) SoF_Skip(2);
		if (ext & 0x02) SoF_Skip(1);
		if (ext & 0x04) SoF_Skip(6);
		if (ext & 0x08) SoF_Skip(6);
		if (ext & 0x10) SoF_Skip(6);
		if (ext & 0x20) SoF_Skip(6);
		if (ext & 0x40) SoF_Skip(2);
		if (ext & 0x80) SoF_Skip(1);
	}
}

static void
SoF_ParseSound(void)
{
	/* OpenMerc (I1a): played through the sound module (SPEC-004) */
	SNDH_ParseSound();
}

static void
SoF_ParseGhoul(const char *what)
{
	int len = MSG_ReadShort(&net_message);

	if ((len < 0) || (net_message.readcount + len > net_message.cursize))
	{
		Com_Error(ERR_DROP, "Bad Ghoul %s packet", what);
		return;
	}

	if (what[0] == 'r')
	{
		CL_Demo_StoreGhoulReliable(net_message.data + net_message.readcount, len); /* OpenMerc (I6h) */
		CL_Ghoul_ParseReliable(net_message.data + net_message.readcount, len); /* I1c: system method 24 */
	}
	else
	{
		CL_Ghoul_ParseUnreliable(net_message.data + net_message.readcount, len); /* I1c: system method 25 */
	}

	SoF_Skip(len); /* ghoul bit stream (SPEC-201 Appendix B) */
}

static void
SoF_ParseRic(void)
{
	int count, h, i;

	count = MSG_ReadByte(&net_message);

	for (i = 0; i < count; i++)
	{
		h = MSG_ReadByte(&net_message);

		if ((h & 0x0F) <= 4)
		{
			switch ((h >> 4) & 3)
			{
				case 0: SoF_Skip(1); break;
				case 1: SoF_Skip(2); break;
				case 2: SoF_Skip(3); break;
				default: SoF_Skip(4); break;
			}
		}
	}
}

static void
SoF_ParseNameCols(void)
{
	int count, covered = 0, b, first, last;

	count = MSG_ReadByte(&net_message);

	while (covered < count)
	{
		b = MSG_ReadByte(&net_message);

		if (b < 0)
		{
			Com_Error(ERR_DROP, "SoF: svc_playernamecols ended early");
			return;
		}

		if (b & 0x80)
		{
			first = MSG_ReadByte(&net_message);
			last = MSG_ReadByte(&net_message);
			covered += (last - first + 1 > 0) ? last - first + 1 : 1;
			HUD_SetNameCols(first, last, b & 0x7F); /* OpenMerc (I6h) */
		}
		else
		{
			first = MSG_ReadByte(&net_message);
			covered++;
			HUD_SetNameCols(first, first, b & 0x7F); /* OpenMerc (I6h) */
		}
	}
}

static void
SoF_ParseEquip(void)
{
	int kind, i, n;

	kind = MSG_ReadByte(&net_message);

	if (kind != 1)
	{
		return;
	}

	for (i = 0; i < 3; i++)
	{
		n = MSG_ReadByte(&net_message);

		while (n-- > 0)
		{
			(void)MSG_ReadString(&net_message);
			(void)MSG_ReadLong(&net_message);
		}
	}
}

static const char *
SoF_PlayerName(int slot)
{
	static char name[64];
	char *p;

	if ((slot < 0) || (slot >= 32))
	{
		return "?";
	}

	Q_strlcpy(name, sof_cs[SOF_CS_PLAYERSKINS + slot], sizeof(name));
	p = strchr(name, '\\');

	if (p)
	{
		*p = 0;
	}

	return name;
}

/* ---------------------------------------------------------------------
 * server message loop (SPEC-201)
 * --------------------------------------------------------------------- */

void
CL_SoF_ParseServerMessage(void)
{
	int cmd, i, n;
	char *s;

	if (cl_shownet->value == 1)
	{
		Com_Printf("%i ", net_message.cursize);
	}
	else if (cl_shownet->value >= 2)
	{
		Com_Printf("------------------\n");
	}

	while (1)
	{
		if (net_message.readcount > net_message.cursize)
		{
			Com_Error(ERR_DROP, "CL_ParseServerMessage: Bad server message");
			return;
		}

		cmd = MSG_ReadByte(&net_message);

		if (cmd == -1)
		{
			break;
		}

		if ((cmd >= SOFSVC_COUNT) || (cmd == sofsvc_bad) || (cmd == sofsvc_unused))
		{
			Com_Error(ERR_DROP, "CL_ParseServerMessage: Illegible server message %d", cmd);
			return;
		}

		if (cl_shownet->value >= 2)
		{
			Com_Printf("%3i:%s\n", net_message.readcount - 1, sofsvc_names[cmd]);
		}

		switch (cmd)
		{
			case sofsvc_temp_entity:
				SoF_ParseTempEntity();
				break;

			case sofsvc_layout:
				HUD_ParseLayout(MSG_ReadString(&net_message)); /* OpenMerc (I6h) */
				break;

			case sofsvc_sound_info:
				/* OpenMerc (I1a): music / ambient / processing values
				   go to the sound module (SPEC-004) */
				n = MSG_ReadByte(&net_message);
				SNDH_ParseSoundInfo(n);
				break;

			case sofsvc_effect:
				SoF_ParseEffect();
				break;

			case sofsvc_equip:
				SoF_ParseEquip();
				break;

			case sofsvc_nop:
				break;

			case sofsvc_disconnect:
				i = SoF_ReadUShort();
				Com_Error(ERR_DISCONNECT, "Server disconnected (string 0x%04x)\n", i);
				return;

			case sofsvc_reconnect:
				Com_Printf("Server disconnected, reconnecting\n");

				if (cls.download)
				{
					fclose(cls.download);
					cls.download = NULL;
				}

				/* SPEC-203 section 5: the handshake restarts from getchallenge */
				CL_SoF_NewAttempt();
				cls.state = ca_connecting;
				cls.connect_time = -99999;
				break;

			case sofsvc_sound:
				SoF_ParseSound();
				break;

			case sofsvc_print:
				i = MSG_ReadByte(&net_message);
				s = MSG_ReadString(&net_message);

				/* OpenMerc: chat (level 3) is printed as is; the Quake II high-bit
				   "green" text maps to Cyrillic glyphs in Russian fonts and to accented
				   letters in the others (SPEC-132) */
				Com_Printf("%s", s);
				con.ormask = 0;
				break;

			case sofsvc_nameprint:
				i = MSG_ReadByte(&net_message);
				n = MSG_ReadByte(&net_message);
				s = MSG_ReadString(&net_message);

				if (HUD_IsIgnored(i))
				{
					break; /* OpenMerc (I6h): ignore list (SPEC-310 section 15) */
				}

				Com_Printf(n ? "(%s) : %s" : "%s : %s", SoF_PlayerName(i), s);
				break;

			case sofsvc_stufftext:
				s = MSG_ReadString(&net_message);
				Com_DPrintf("stufftext: %s\n", s);
				Cbuf_AddText(s);
				break;

			case sofsvc_serverdata:
				Cbuf_Execute();
				SoF_ParseServerData();
				break;

			case sofsvc_configstring:
				SoF_ParseConfigString();
				break;

			case sofsvc_spawnbaseline:
				SoF_ParseBaseline();
				break;

			case sofsvc_centerprint:
				SCR_CenterPrint(MSG_ReadString(&net_message));
				break;

			case sofsvc_captionprint:
				SCR_CaptionPrint(CL_StringById(SoF_ReadUShort())); /* OpenMerc (I6h) */
				break;

			case sofsvc_download:
				n = MSG_ReadShort(&net_message);
				SoF_Skip(1);

				if (n > 0)
				{
					SoF_Skip(n);
				}

				break;

			case sofsvc_playerinfo:
			case sofsvc_packetentities:
			case sofsvc_deltapacketentities:
				Com_Error(ERR_DROP, "Out of place frame data");
				return;

			case sofsvc_frame:
				SoF_ParseFrame();
				break;

			case sofsvc_culledevent:
			{
				/* OpenMerc (I5): SPEC-201 section 24 */
				int ev = MSG_ReadByte(&net_message);
				int parm = MSG_ReadByte(&net_message);
				int dist = MSG_ReadByte(&net_message);
				vec3_t dir;

				CL_SoF_DirVector(MSG_ReadByte(&net_message), dir);
				CLFX_CulledEvent(ev, parm, dist, dir);
				break;
			}

			case sofsvc_damagetexture:
				SoF_Skip(3);
				break;

			case sofsvc_ghoulreliable:
				SoF_ParseGhoul("reliable");
				break;

			case sofsvc_ghoulunreliable:
				SoF_ParseGhoul("unreliable");
				break;

			case sofsvc_ric:
				CL_Player_ParseEvents(); /* I1d: inventory events (SPEC-003) */
				break;

			case sofsvc_restart_predn:
				CL_Player_ParseRestart(); /* I1d: new prediction inventory */
				break;

			case sofsvc_rebuild_pred_inv:
				CL_Player_ParseFullState(); /* I1d: 640-byte full inventory state */
				break;

			case sofsvc_countdown:
				HUD_SetCountdown(MSG_ReadLong(&net_message)); /* OpenMerc (I6h) */
				break;

			case sofsvc_cinprintf:
			{
				/* OpenMerc (I6h): typewriter print (SPEC-201 section 32) */
				int cx = MSG_ReadShort(&net_message);
				int cy = MSG_ReadShort(&net_message);
				int speed = MSG_ReadByte(&net_message);

				SCR_TypewriterPrint(cx, cy, speed, MSG_ReadString(&net_message));
				break;
			}

			case sofsvc_playernamecols:
				SoF_ParseNameCols();
				break;

			case sofsvc_sp_print:
				i = SoF_ReadUShort();
				CL_SP_Print(i, NULL, 0, false); /* OpenMerc (I6h) */
				break;

			case sofsvc_removeconfigstring:
				SoF_RemoveConfigString();
				break;

			case sofsvc_sp_print_data_1:
			case sofsvc_sp_print_obit:
				i = SoF_ReadUShort();
				n = MSG_ReadByte(&net_message);
				SoF_Skip(n);
				/* OpenMerc (I6h): string-package print (SPEC-201 section 34) */
				CL_SP_Print(i, net_message.data + net_message.readcount - n, n,
						cmd == sofsvc_sp_print_obit);
				break;

			case sofsvc_sp_print_data_2:
				i = SoF_ReadUShort();
				n = MSG_ReadShort(&net_message);
				SoF_Skip(n);
				CL_SP_Print(i, net_message.data + net_message.readcount - n, n, false);
				break;

			case sofsvc_welcomeprint:
				if (!sof_welcome_shown && sof_cs[SOF_CS_WELCOME][0])
				{
					/* OpenMerc (I6h): the centre print echoes to the console
					   itself (SPEC-310 6); the local copy is emptied
					   (SPEC-201 38), so demo headers do not carry it */
					SCR_CenterPrint(sof_cs[SOF_CS_WELCOME]);
					sof_cs[SOF_CS_WELCOME][0] = 0;
					sof_welcome_shown = true;
				}

				break;

			case sofsvc_force_con_notify:
				SCR_ClearPrints(); /* OpenMerc (I6h) */
				break;
		}

		SoF_CheckRead(sofsvc_names[cmd]);
	}

	CL_AddNetgraph();
}

/* ---------------------------------------------------------------------
 * user commands (SPEC-202 section 6)
 * --------------------------------------------------------------------- */

void
CL_SoF_FinalizeCmd(usercmd_t *cmd, int lean, qboolean run, qboolean altattack,
		qboolean extra1, qboolean extra2)
{
	sof_cmdext_t *x = &sof_cmdext[cls.netchan.outgoing_sequence & (CMD_BACKUP - 1)];
	int i;

	if (run)
	{
		cmd->buttons |= SOF_BUTTON_RUN;
	}

	if (altattack)
	{
		cmd->buttons |= SOF_BUTTON_ALTATTACK;
	}

	if (extra1)
	{
		cmd->buttons |= SOF_BUTTON_WEAPONEXTRA1;
	}

	if (extra2)
	{
		cmd->buttons |= SOF_BUTTON_WEAPONEXTRA2;
	}

	/* the wire keeps 12 bits per angle; predict with what the server gets */
	for (i = 0; i < 3; i++)
	{
		cmd->angles[i] = (short)((unsigned short)cmd->angles[i] & 0xFFF0);
	}

	cmd->forwardmove = (short)Q_clamp(cmd->forwardmove, -200, 200);
	cmd->sidemove = (short)Q_clamp(cmd->sidemove, -160, 160);
	cmd->upmove = (short)Q_clamp(cmd->upmove, -200, 200);

	x->lean = lean;
	x->firetime = 0; /* no predicted weapon fire yet */
	x->altfiretime = 0;
}

typedef struct
{
	byte *data;
	int maxbits;
	int bit;
	qboolean overflowed;
} sof_bitbuf_t;

static void
SoF_WriteBits(sof_bitbuf_t *b, unsigned value, int n)
{
	int i;

	for (i = 0; i < n; i++)
	{
		if (b->bit >= b->maxbits)
		{
			b->overflowed = true;
			return;
		}

		if (value & (1u << i))
		{
			b->data[b->bit >> 3] |= 1 << (b->bit & 7);
		}

		b->bit++;
	}
}

static unsigned
SoF_FloatBits(float f)
{
	unsigned u;

	memcpy(&u, &f, sizeof(u));
	return u;
}

static void
SoF_WriteUsercmd(sof_bitbuf_t *b, const usercmd_t *from, const sof_cmdext_t *fromx,
		const usercmd_t *to, const sof_cmdext_t *tox)
{
	int i, v, r;
	const short *moves_to[3] = {&to->forwardmove, &to->sidemove, &to->upmove};
	const short *moves_from[3] = {&from->forwardmove, &from->sidemove, &from->upmove};
	static const int limit[3] = {200, 160, 200};

	for (i = 0; i < 3; i++)
	{
		v = ((unsigned short)to->angles[i]) >> 4;
		r = ((unsigned short)from->angles[i]) >> 4;

		if (v != r)
		{
			SoF_WriteBits(b, 1, 1);
			SoF_WriteBits(b, v, 12);
		}
		else
		{
			SoF_WriteBits(b, 0, 1);
		}
	}

	for (i = 0; i < 3; i++)
	{
		v = Q_clamp(*moves_to[i], -limit[i], limit[i]);
		r = Q_clamp(*moves_from[i], -limit[i], limit[i]);

		if (v != r)
		{
			SoF_WriteBits(b, 1, 1);
			SoF_WriteBits(b, v + 510, 10);
		}
		else
		{
			SoF_WriteBits(b, 0, 1);
		}
	}

	if (to->buttons != from->buttons)
	{
		SoF_WriteBits(b, 1, 1);
		SoF_WriteBits(b, to->buttons, 8);
	}
	else
	{
		SoF_WriteBits(b, 0, 1);
	}

	/* lean is always sent */
	SoF_WriteBits(b, tox->lean != 0, 1);

	if (tox->lean)
	{
		SoF_WriteBits(b, tox->lean < 0, 1);
	}

	if ((to->lightlevel >> 3) != (from->lightlevel >> 3))
	{
		SoF_WriteBits(b, 1, 1);
		SoF_WriteBits(b, to->lightlevel >> 3, 5);
	}
	else
	{
		SoF_WriteBits(b, 0, 1);
	}

	SoF_WriteBits(b, to->msec, 8);

	if (SoF_FloatBits(tox->firetime) != SoF_FloatBits(fromx->firetime))
	{
		SoF_WriteBits(b, 1, 1);
		SoF_WriteBits(b, SoF_FloatBits(tox->firetime), 32);
	}
	else
	{
		SoF_WriteBits(b, 0, 1);
	}

	if (SoF_FloatBits(tox->altfiretime) != SoF_FloatBits(fromx->altfiretime))
	{
		SoF_WriteBits(b, 1, 1);
		SoF_WriteBits(b, SoF_FloatBits(tox->altfiretime), 32);
	}
	else
	{
		SoF_WriteBits(b, 0, 1);
	}
}

/* SPEC-202 6.3 */
static int
SoF_MoveChecksum(const byte *region, int len, unsigned sequence)
{
	byte tmp[64 + 4];
	unsigned p;

	if (len > 60)
	{
		len = 60;
	}

	memcpy(tmp, region, len);
	p = sequence % 2996;
	tmp[len + 0] = sof_dir_bytes[p] ^ (sequence & 0xFF);
	tmp[len + 1] = sof_dir_bytes[p + 1];
	tmp[len + 2] = sof_dir_bytes[p + 2] ^ ((sequence >> 8) & 0xFF);
	tmp[len + 3] = sof_dir_bytes[p + 3];

	return Com_BlockChecksum(tmp, len + 4) & 0xFF;
}

void
CL_SoF_WriteMove(sizebuf_t *buf, qboolean nodelta)
{
	byte bits[96];
	sof_bitbuf_t bb;
	usercmd_t nullcmd;
	sof_cmdext_t nullx;
	int i0, i1, i2, checksumIndex;
	unsigned seq = cls.netchan.outgoing_sequence;

	memset(bits, 0, sizeof(bits));
	memset(&nullcmd, 0, sizeof(nullcmd));
	memset(&nullx, 0, sizeof(nullx));
	bb.data = bits;
	bb.maxbits = sizeof(bits) * 8;
	bb.bit = 0;
	bb.overflowed = false;

	MSG_WriteByte(buf, clc_move);
	checksumIndex = buf->cursize;
	MSG_WriteByte(buf, 0);

	if (nodelta || !cl.frame.valid || cls.demowaiting)
	{
		MSG_WriteLong(buf, -1);
	}
	else
	{
		MSG_WriteLong(buf, cl.frame.serverframe);
	}

	/* commands of sequences s-2, s-1 and s */
	i0 = (seq - 2) & (CMD_BACKUP - 1);
	i1 = (seq - 1) & (CMD_BACKUP - 1);
	i2 = seq & (CMD_BACKUP - 1);

	SoF_WriteUsercmd(&bb, &nullcmd, &nullx, &cl.cmds[i0], &sof_cmdext[i0]);
	SoF_WriteUsercmd(&bb, &cl.cmds[i0], &sof_cmdext[i0], &cl.cmds[i1], &sof_cmdext[i1]);
	SoF_WriteUsercmd(&bb, &cl.cmds[i1], &sof_cmdext[i1], &cl.cmds[i2], &sof_cmdext[i2]);

	if (bb.overflowed)
	{
		Com_Printf("CL_SoF_WriteMove: overflow\n");
	}

	SZ_Write(buf, bits, (bb.bit + 7) >> 3);

	buf->data[checksumIndex] = SoF_MoveChecksum(buf->data + checksumIndex + 1,
			buf->cursize - checksumIndex - 1, seq);
}

/* ---------------------------------------------------------------------
 * drawing (placeholders until the Ghoul renderer exists)
 * --------------------------------------------------------------------- */

void
CL_SoF_AddPacketEntities(frame_t *frame)
{
	entity_t ent;
	entity_state_t *s1;
	centity_t *cent;
	int pnum, i;
	vec3_t mins, maxs;

	for (pnum = 0; pnum < frame->num_entities; pnum++)
	{
		s1 = &cl_parse_entities[(frame->parse_entities + pnum) & (MAX_PARSE_ENTITIES - 1)];

		if ((s1->number <= 0) || (s1->number >= cl_numentities))
		{
			continue;
		}

		if (s1->renderfx & SOF_RF_SOUNDONLY)
		{
			continue; /* sent only for its sound */
		}

		if ((s1->number == cl.playernum + 1) && !((s1->renderfx & SOF_RF_GHOUL) && CL_Ghoul_Active()))
		{
			continue; /* our own body (I1c: a Ghoul one is drawn shadow-only, below) */
		}

		cent = &cl_entities[s1->number];
		memset(&ent, 0, sizeof(ent));

		if (s1->renderfx & SOF_RF_NOLERP)
		{
			VectorCopy(cent->current.origin, ent.origin);
		}
		else
		{
			for (i = 0; i < 3; i++)
			{
				ent.origin[i] = cent->prev.origin[i] + cl.lerpfrac *
					(cent->current.origin[i] - cent->prev.origin[i]);
			}
		}

		VectorCopy(ent.origin, ent.oldorigin);

		/* I1c: angles (negated with a secondary model index, the real-time auto-rotate yaw), frame (with the
		   auto-animation effect bits), old frame, back-lerp, alpha (SPEC-AMEND-006 I1c.2, I1c.7) */
		CL_Ghoul_EntityValues(&ent, s1);

		/* inline brush models are real geometry */
		if ((s1->modelindex > 0) && (s1->modelindex < MAX_MODELS) &&
			(sof_cs[SOF_CS_MODELS + s1->modelindex][0] == '*'))
		{
			ent.model = cl.model_draw[s1->modelindex];

			if (ent.model)
			{
				V_AddEntity(&ent);
			}

			continue;
		}

		if (!s1->modelindex && !s1->modelindex2 && !(s1->renderfx & SOF_RF_GHOUL))
		{
			continue; /* nothing visible (sound emitters, effect anchors) */
		}

		/* I1c: Ghoul models through the client Ghoul system (SPEC-002 entity, frame = handle) */
		if ((s1->renderfx & SOF_RF_GHOUL) && CL_Ghoul_AddEntity(&ent, s1, s1->number == cl.playernum + 1))
		{
			continue;
		}

		/* Ghoul model without a Ghoul renderer: the renderer's null model marks the spot */
		ent.model = NULL;
		V_AddEntity(&ent);

		if ((s1->modelindex == 255) && s1->solid && (s1->solid != 31))
		{
			/* players: a translucent column over the bounding box */
			CL_SoF_SolidBox(s1->solid, mins, maxs);
			memset(&ent.angles, 0, sizeof(ent.angles));
			VectorCopy(ent.origin, ent.oldorigin);
			ent.origin[2] += mins[2];
			ent.oldorigin[2] += maxs[2];
			ent.flags = RF_BEAM | RF_TRANSLUCENT;
			ent.frame = (int)(maxs[0] > 4 ? maxs[0] : 16);
			ent.skinnum = 0xF2;
			ent.alpha = 0.45f;
			V_AddEntity(&ent);
		}
	}
}
