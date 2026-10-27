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
 * Server console commands.
 *
 * OpenMerc: the SoF 1.07f server command set: Quake II's operator
 * commands, the game-module commands savesleft/getgamestats/updateinven
 * (run with the player-module context at 0), gserver and adddownload.
 * The address filter, deathmatch-flag and bot commands come from the
 * engine core (SoF_InitAdminCommands).
 *
 * Implements: SPEC-001 ("savesleft", "getgamestats", "updateinven",
 *             Gametype in status), SPEC-003 (context flag), SPEC-010
 *             (gserver, adddownload), SPEC-302 (server command list),
 *             SPEC-313 s3 (getgamestats without a game)
 *
 * =======================================================================
 */

#include "header/server.h"
#include "../common/header/sof/engine.h"
#include "../common/header/sof/savegame.h"

static qboolean sv_newgame; /* the current map command is 'map' */

/* hooks of the engine core's server commands (common/sof_admin.c) */
extern void (*sof_game_savesleft)(void);
extern void (*sof_game_getgamestats)(void);
extern void (*sof_game_updateinven)(void);
extern void (*sof_ghoul_command)(const char *text);

/*
 * Sets sv_client and sv_player to the player with idnum Cmd_Argv(1)
 */
static qboolean
SV_SetPlayer(void)
{
	client_t *cl;
	int i;
	int idnum;
	char *s;

	if (Cmd_Argc() < 2)
	{
		return false;
	}

	s = Cmd_Argv(1);

	/* numeric values are just slot numbers */
	if ((s[0] >= '0') && (s[0] <= '9'))
	{
		idnum = (int)strtol(Cmd_Argv(1), (char **)NULL, 10);

		if ((idnum < 0) || (idnum >= svs.numclients))
		{
			Com_Printf("Bad client slot: %i\n", idnum);
			return false;
		}

		sv_client = &svs.clients[idnum];
		sv_player = CL_EDICT(sv_client);

		if (!sv_client->state)
		{
			Com_Printf("Client %i is not active\n", idnum);
			return false;
		}

		return true;
	}

	/* check for a name match */
	for (i = 0, cl = svs.clients; i < svs.numclients; i++, cl++)
	{
		if (!cl->state)
		{
			continue;
		}

		if (!strcmp(cl->name, s))
		{
			sv_client = cl;
			sv_player = CL_EDICT(cl);
			return true;
		}
	}

	Com_Printf("Userid %s is not on the server\n", s);
	return false;
}

/*
 * Puts the server in demo mode on a specific map/cinematic
 */
static void
SV_DemoMap_f(void)
{
	if (Cmd_Argc() != 2)
	{
		Com_Printf("USAGE: demomap <demoname.dm2>\n");
		return;
	}

	SV_Map(true, Cmd_Argv(1), false);
}

/*
 * Goes to a new map keeping the game state
 */
static void
SV_GameMap_f(void)
{
	char mapcmd[MAX_TOKEN_CHARS];

	if (Cmd_Argc() != 2)
	{
		Com_Printf("USAGE: gamemap <map>\n");
		return;
	}

	/* copied first: starting the game module and the map run commands that
	   tokenize again (Cmd_Argv would then return their words) */
	Q_strlcpy(mapcmd, Cmd_Argv(1), sizeof(mapcmd));

	Com_DPrintf("SV_GameMap(%s)\n", mapcmd);

	/* start up the next map */
	SV_Map(false, mapcmd, false);

	/* archive server state */
	Q_strlcpy(svs.mapcmd, mapcmd, sizeof(svs.mapcmd));

	/* SPEC-303: a single-player level start writes the automatic save
	   ('map' deletes the old one first, 'gamemap' does not) */
	if ((sv.state == ss_game) && !Cvar_VariableValue("deathmatch"))
	{
		SG_AutoSave(sv_newgame);
	}

	sv_newgame = false;
}

/*
 * A new game on the given map
 */
static void
SV_Map_f(void)
{
	char *map;
	char expanded[MAX_QPATH];

	if (Cmd_Argc() != 2)
	{
		Com_Printf("USAGE: map <mapname>\n");
		return;
	}

	/* if not a demo or cinematic, check to make sure the level exists */
	map = Cmd_Argv(1);

	if (!strstr(map, ".") && !strstr(map, "$") && (*map != '*'))
	{
		Com_sprintf(expanded, sizeof(expanded), "maps/%s.bsp", map);

		if (!FS_FileExists(expanded))
		{
			Com_Printf("Can't find %s\n", expanded);
			return;
		}
	}

	sv.state = ss_dead; /* a new game: the spawn count is randomised again */
	sv_newgame = true;
	SV_GameMap_f();
}

/*
 * Lists available maps for user to load.
 */
static void
SV_ListMaps_f(void)
{
	strlist_t userMapNames;
	int i;
	char *mapName, *lastsep;

	Com_Printf("\n");

	userMapNames = FS_ListFiles2("maps/*.bsp", 0, 0);

	for (i = 0; i < userMapNames.num; i++)
	{
		if ((lastsep = strrchr(userMapNames.data[i], '/')))
		{
			mapName = lastsep + 1;
		}
		else
		{
			mapName = userMapNames.data[i];
		}

		mapName = strtok(mapName, ".");

		Com_Printf("%s\n", mapName);
	}

	StrList_Free(&userMapNames);
}

/*
 * Kick a user off of the server
 */
static void
SV_Kick_f(void)
{
	if (!svs.initialized)
	{
		Com_Printf("No server running.\n");
		return;
	}

	if (Cmd_Argc() != 2)
	{
		Com_Printf("Usage: kick <userid>\n");
		return;
	}

	if (!SV_SetPlayer())
	{
		return;
	}

	if ((sv_client->state == cs_spawned) && *sv_client->name)
	{
		SV_BroadcastPrintf(PRINT_HIGH, "%s was kicked\n", sv_client->name);
	}

	SV_DropClientWithReason(sv_client, SV_STR_KICKED);
	sv_client->lastmessage = svs.realtime; /* min case there is a funny zombie */
}

/* OpenMerc test command (not in the original): sv_stufftext <userid> <text> sends the text as
   svc_stufftext to that client's console, so black-box tests can drive any client by rcon. */
static void
SV_StuffText_f(void)
{
	if (!svs.initialized)
	{
		Com_Printf("No server running.\n");
		return;
	}

	if (Cmd_Argc() < 3)
	{
		Com_Printf("Usage: sv_stufftext <userid> <text>\n");
		return;
	}

	if (!SV_SetPlayer())
	{
		return;
	}

	MSG_WriteByte(&sv_client->netchan.message, sofsvc_stufftext);
	MSG_WriteString(&sv_client->netchan.message, va("%s\n", Cmd_Args() + strlen(Cmd_Argv(1)) + 1));
}

/* OpenMerc test command (not in the original): sv_setpos <x> <y> <z> [yaw] [pitch] puts the first
   player there at rest, with either game module (black-box collision walks need it in mode A, where
   the original game has no position command). Needs cheats 1, like the game's own cheats. */
static void
SV_SetPos_f(void)
{
	edict_t *ent;
	sof_gclient_t *gc;
	int k;

	if (!svs.initialized || !svs.clients || (svs.clients[0].state != cs_spawned))
	{
		Com_Printf("No player in game.\n");
		return;
	}

	if (!Cvar_VariableValue("cheats"))
	{
		Com_Printf("sv_setpos needs cheats 1\n");
		return;
	}

	if (Cmd_Argc() < 4)
	{
		Com_Printf("Usage: sv_setpos <x> <y> <z> [yaw] [pitch]\n");
		return;
	}

	ent = CL_EDICT(&svs.clients[0]);
	gc = (sof_gclient_t *)ent->client;

	if (!gc)
	{
		return;
	}

	for (k = 0; k < 3; k++)
	{
		const float v = (float)atof(Cmd_Argv(1 + k));

		ent->s.origin[k] = v;
		gc->ps.pmove.origin[k] = (int16_t)(v * 8.0f);
		gc->ps.pmove.velocity[k] = 0;
	}

	if (Cmd_Argc() > 4)
	{
		gc->ps.pmove.delta_angles[YAW] = (int16_t)(ANGLE2SHORT(atof(Cmd_Argv(4))) -
			svs.clients[0].lastcmd.angles[YAW]);
	}

	if (Cmd_Argc() > 5)
	{
		gc->ps.pmove.delta_angles[PITCH] = (int16_t)(ANGLE2SHORT(atof(Cmd_Argv(5))) -
			svs.clients[0].lastcmd.angles[PITCH]);
	}

	SV_LinkEdict(ent);
	Com_Printf("sv_setpos %.1f %.1f %.1f\n", ent->s.origin[0], ent->s.origin[1], ent->s.origin[2]);
}

static void
SV_Status_f(void)
{
	int i, j;
	size_t l;
	client_t *cl;
	char *s;
	int ping;

	if (!svs.clients)
	{
		Com_Printf("No server running.\n");
		return;
	}

	Com_Printf("map              : %s\n", sv.name);
	Com_Printf("Gametype: %s\n", SV_GameTypeName());

	Com_Printf("num score ping name            lastmsg address               qport \n");
	Com_Printf("--- ----- ---- --------------- ------- --------------------- ------\n");

	for (i = 0, cl = svs.clients; i < svs.numclients; i++, cl++)
	{
		if (!cl->state)
		{
			continue;
		}

		Com_Printf("%3i ", i);
		Com_Printf("%5i ", SV_ClientScore(cl));

		if (cl->state == cs_connected)
		{
			Com_Printf("CNCT ");
		}
		else if (cl->state == cs_zombie)
		{
			Com_Printf("ZMBI ");
		}
		else
		{
			ping = cl->ping < 9999 ? cl->ping : 9999;
			Com_Printf("%4i ", ping);
		}

		Com_Printf("%s", cl->name);
		l = 16 - strlen(cl->name);

		for (j = 0; j < (int)l; j++)
		{
			Com_Printf(" ");
		}

		Com_Printf("%7i ", svs.realtime - cl->lastmessage);

		s = NET_AdrToString(cl->netchan.remote_address);
		Com_Printf("%s", s);
		l = 22 - strlen(s);

		for (j = 0; j < (int)l; j++)
		{
			Com_Printf(" ");
		}

		Com_Printf("%5i", cl->netchan.qport);

		Com_Printf("\n");
	}

	Com_Printf("\n");
}

static void
SV_ConSay_f(void)
{
	client_t *client;
	int j;
	char *p;
	char text[1024];

	if (Cmd_Argc() < 2)
	{
		return;
	}

	if (!svs.initialized)
	{
		Com_Printf("No server running.\n");
		return;
	}

	strcpy(text, "console: ");
	p = Cmd_Args();

	if (*p == '"')
	{
		p++;
		p[strlen(p) - 1] = 0;
	}

	Q_strlcat(text, p, sizeof(text));

	for (j = 0, client = svs.clients; j < svs.numclients; j++, client++)
	{
		if (client->state != cs_spawned)
		{
			continue;
		}

		SV_ClientPrintf(client, PRINT_CHAT, "%s\n", text);
	}
}

static void
SV_Heartbeat_f(void)
{
	SV_GameSpy_ForceHeartbeat();
}

/*
 * Examine or change the serverinfo string
 */
static void
SV_Serverinfo_f(void)
{
	Com_Printf("Server info settings:\n");
	Info_Print(Cvar_Serverinfo());
}

/*
 * Examine all a users info strings
 */
static void
SV_DumpUser_f(void)
{
	if (!svs.initialized)
	{
		Com_Printf("No server running.\n");
		return;
	}

	if (Cmd_Argc() != 2)
	{
		Com_Printf("Usage: dumpuser <userid>\n");
		return;
	}

	if (!SV_SetPlayer())
	{
		return;
	}

	Com_Printf("userinfo\n");
	Com_Printf("--------\n");
	Info_Print(sv_client->userinfo);
}

/*
 * Begins server demo recording.  Every entity and every message will be
 * recorded, but no playerinfo will be stored.  Primarily for demo merging.
 */
static void
SV_ServerRecord_f(void)
{
	char name[MAX_OSPATH];
	byte buf_data[32768];
	sizebuf_t buf;
	int len;
	int i;

	if (Cmd_Argc() != 2)
	{
		Com_Printf("serverrecord <demoname>\n");
		return;
	}

	if (svs.demofile)
	{
		Com_Printf("Already recording.\n");
		return;
	}

	if (sv.state != ss_game)
	{
		Com_Printf("You must be in a level to record.\n");
		return;
	}

	if (strstr(Cmd_Argv(1), "..") ||
		strstr(Cmd_Argv(1), "/") ||
		strstr(Cmd_Argv(1), "\\"))
	{
		Com_Printf("Illegal filename.\n");
		return;
	}

	/* open the demo file */
	Com_sprintf(name, sizeof(name), "%s/demos/%s.dm2", FS_Gamedir(), Cmd_Argv(1));

	Com_Printf("recording to %s.\n", name);
	FS_CreatePath(name);
	svs.demofile = Q_fopen(name, "wb");

	if (!svs.demofile)
	{
		Com_Printf("ERROR: couldn't open.\n");
		return;
	}

	/* setup a buffer to catch all multicasts */
	SZ_Init(&svs.demo_multicast, svs.demo_multicast_buf,
			sizeof(svs.demo_multicast_buf));

	/* write a single giant fake message with all the startup info */
	SZ_Init(&buf, buf_data, sizeof(buf_data));

	MSG_WriteByte(&buf, sofsvc_serverdata);
	MSG_WriteLong(&buf, SV_PROTOCOL_VERSION);
	MSG_WriteLong(&buf, svs.spawncount);
	MSG_WriteByte(&buf, 2); /* demos are always attract loops */
	MSG_WriteByte(&buf, (int)Cvar_VariableValue("deathmatch"));
	MSG_WriteString(&buf, (char *)Cvar_VariableString("gamedir"));
	MSG_WriteShort(&buf, -1);

	/* send full levelname */
	MSG_WriteString(&buf, sv.configstrings[SOF_CS_NAME]);

	for (i = 0; i < OM_MAX_CONFIGSTRINGS; i++)
	{
		if (sv.configstrings[i][0])
		{
			SV_WriteConfigstringMsg(&buf, i, sv.configstrings[i]);

			if (buf.cursize + 72 >= buf.maxsize)
			{
				Com_Printf("not enough buffer space available.\n");
				fclose(svs.demofile);
				svs.demofile = NULL;
				return;
			}
		}
	}

	/* write it to the demo file */
	Com_DPrintf("signon message length: %i\n", buf.cursize);
	len = LittleLong(buf.cursize);
	fwrite(&len, 4, 1, svs.demofile);
	fwrite(buf.data, buf.cursize, 1, svs.demofile);
}

/*
 * Ends server demo recording
 */
static void
SV_ServerStop_f(void)
{
	if (!svs.demofile)
	{
		Com_Printf("Not doing a serverrecord.\n");
		return;
	}

	fclose(svs.demofile);
	svs.demofile = NULL;
	Com_Printf("Recording completed.\n");
}

/*
 * Kick everyone off, possibly in preparation for a new game
 */
static void
SV_KillServer_f(void)
{
	if (!svs.initialized)
	{
		return;
	}

	SV_Shutdown("Server was killed.\n", false);
	NET_Config(false); /* close network sockets */
}

/* ---- game-module commands (SPEC-001) ---- */

static void
SV_SavesLeft_f(void)
{
	if (ge && ge->SavesLeft)
	{
		ge->SavesLeft();
	}
}

static void
SV_GetGameStats_f(void)
{
	static const char *const stats[] = {
		"stat_playedtime_base", "stat_guyskilled", "stat_friendlieskilled",
		"stat_throatshots", "stat_nutshots", "stat_headshots", "stat_gibs",
		"stat_savesused", "stat_playedtime", "stat_cashearned"
	};
	int i;

	if (ge && ge->GameStats)
	{
		SV_GameCallWithPlayerContext(ge->GameStats);
		return;
	}

	/* SPEC-313 s3: without a game module the ten cvars become "" */
	for (i = 0; i < (int)(sizeof(stats) / sizeof(stats[0])); i++)
	{
		Cvar_Set(stats[i], "");
	}
}

static void
SV_UpdateInven_f(void)
{
	if (ge && ge->UpdateInven)
	{
		SV_GameCallWithPlayerContext(ge->UpdateInven);
	}
}

/* gserver: the engine core upper-cases the arguments; they go to the model
   system's debug commands (SPEC-313 s3) */
static void
SV_GserverText(const char *text)
{
	static char args[8][64];
	const char *argv[8];
	int argc = 1;
	const char *p = text;

	Q_strlcpy(args[0], "GSERVER", sizeof(args[0]));
	argv[0] = args[0];

	while (*p && (argc < 8))
	{
		int n = 0;

		while (*p == ' ')
		{
			p++;
		}

		if (!*p)
		{
			break;
		}

		while (*p && (*p != ' ') && (n < 63))
		{
			args[argc][n++] = *p++;
		}

		args[argc][n] = 0;
		argv[argc] = args[argc];
		argc++;
	}

	if ((argc > 1) && (!strcmp(args[1], "STATS") || !strcmp(args[1], "SUMMARY")))
	{
		SV_Ghoul_StatsPrint(); /* call counters of the stub, if active */
	}

	SV_Ghoul_Command(argc, argv);
}

/* adddownload <file>: open and close the file so it is offered (system method 22) */
static void
SV_AddDownload_f(void)
{
	if (Cmd_Argc() < 2)
	{
		return;
	}

	SV_Ghoul_TouchFile(Cmd_Argv(1));
}

void
SV_InitOperatorCommands(void)
{
	Cmd_AddCommand("heartbeat", SV_Heartbeat_f);
	Cmd_AddCommand("kick", SV_Kick_f);
	Cmd_AddCommand("status", SV_Status_f);
	Cmd_AddCommand("serverinfo", SV_Serverinfo_f);
	Cmd_AddCommand("dumpuser", SV_DumpUser_f);

	Cmd_AddCommand("map", SV_Map_f);
	Cmd_AddCommand("listmaps", SV_ListMaps_f);
	Cmd_AddCommand("demomap", SV_DemoMap_f);
	Cmd_AddCommand("gamemap", SV_GameMap_f);

	if (dedicated->value)
	{
		Cmd_AddCommand("say", SV_ConSay_f);
	}

	Cmd_AddCommand("serverrecord", SV_ServerRecord_f);
	Cmd_AddCommand("serverstop", SV_ServerStop_f);

	Cmd_AddCommand("killserver", SV_KillServer_f);
	Cmd_AddCommand("sv_tracetest", SV_TraceTest_f); /* OpenMerc debugging */
	Cmd_AddCommand("sv_stufftext", SV_StuffText_f); /* OpenMerc tests */
	Cmd_AddCommand("sv_setpos", SV_SetPos_f); /* OpenMerc tests */

	/* savesleft, getgamestats, updateinven, gserver, adddownload and the
	   address filter, deathmatch-flag and bot commands are registered by
	   the engine core (SoF_InitAdminCommands); the server answers them
	   through the core's hooks */
	sof_game_savesleft = SV_SavesLeft_f;
	sof_game_getgamestats = SV_GetGameStats_f;
	sof_game_updateinven = SV_UpdateInven_f;
	sof_ghoul_command = SV_GserverText;
	(void)SV_AddDownload_f;
}
