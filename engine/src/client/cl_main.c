/*
 * Copyright (C) 1997-2001 Id Software, Inc.
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
 * This is the clients main loop as well as some miscelangelous utility
 * and support functions
 *
 * =======================================================================
 *
 * OpenMerc: "precache <sc>" from a SoF server loads the level and answers
 * with "sv_precache <sc>"; cl_sof.c is initialised here.
 * Implements: SPEC-203
 *
 * OpenMerc (I1a): config.cfg in the SoF format, the sound module starts
 * after the video (SoF order), shutdown flag for the sound struct.
 * Implements: SPEC-301 (Behaviour 1, 11), SPEC-004
 */

#include <errno.h>
#include "header/client.h"
#include "header/sof_sound.h"
#include "header/sofclient.h" /* OpenMerc (I6h) */
#include "input/header/input.h"
#include "fx/cl_fx.h" /* OpenMerc (I5) */
#include "header/cl_ghoul.h" /* I1c */
#include "header/cl_player.h" /* I1d */

void CL_ForwardToServer_f(void);
void CL_Changing_f(void);
void CL_Reconnect_f(void);
void CL_Connect_f(void);
void CL_Rcon_f(void);
void CL_Packet_f(void);
void CL_CheckForResend(void);

cvar_t *rcon_client_password;
cvar_t *rcon_address;

cvar_t *cl_noskins;
cvar_t *cl_footsteps;
cvar_t *cl_timeout;
cvar_t *cl_predict;
cvar_t *cl_showfps;
cvar_t *cl_showspeed;
cvar_t *cl_gun;
cvar_t *cl_add_particles;
cvar_t *cl_add_lights;
cvar_t *cl_add_entities;
cvar_t *cl_add_blend;
cvar_t *cl_kickangles;
cvar_t *cl_laseralpha;
cvar_t *cl_nodownload_list;

cvar_t *cl_shownet;
cvar_t *cl_showmiss;
cvar_t *cl_showclamp;

cvar_t *cl_paused;
cvar_t *cl_loadpaused;
cvar_t *cl_audiopaused;
cvar_t *cl_unpaused_scvis;

cvar_t *cl_lightlevel;
cvar_t *cl_r1q2_lightstyle;
cvar_t *cl_limitsparksounds;

/* userinfo */
cvar_t *name;
cvar_t *skin;
cvar_t *rate;
cvar_t *fov;
cvar_t *horplus;
cvar_t *windowed_mouse;
cvar_t *hand;
cvar_t *gender;
cvar_t *gender_auto;


cvar_t *cl_vwep;

client_static_t cls;
client_state_t cl;

/* initialize the entities array to at least this many entities */
#define ALLOC_ENTITIES_MIN 32

centity_t *cl_entities;
int cl_numentities;

entity_state_t cl_parse_entities[MAX_PARSE_ENTITIES];

/*Evil hack against too many power screen and power
  shield impact sounds. For example if the player
  fires his shotgun onto a Brain. */
int num_power_sounds;

/* Used to communicate if we entered paused mode
   during server connect or if we were already in
   it. */
qboolean paused_at_load;

extern cvar_t *allow_download;
extern cvar_t *allow_download_players;
extern cvar_t *allow_download_models;
extern cvar_t *allow_download_sounds;
extern cvar_t *allow_download_maps;

void
CL_ClearEntities(void)
{
	if (cl_entities)
	{
		Z_Free(cl_entities);
		cl_entities = NULL;
	}

	cl_numentities = 0;
}

centity_t *
CL_AllocEntity(int entnum)
{
	int nextpow2;

	if ((entnum < 0) || (entnum > MAX_CL_ENTNUM))
	{
		return NULL;
	}

	if (entnum >= cl_numentities)
	{
		nextpow2 = (cl_numentities || (entnum >= ALLOC_ENTITIES_MIN)) ?
			(int)NextPow2gt(entnum) : ALLOC_ENTITIES_MIN;

		cl_entities = Z_Realloc(cl_entities, nextpow2 * sizeof(centity_t));
		cl_numentities = nextpow2;
	}

	return &cl_entities[entnum];
}

/*
 * Returns max clients in the current session
 * Returns 1 if the value is bad (<= 0) or the configstring is blank
 */
int
CL_MaxClients(void)
{
	int i = 0;

	sscanf(cl.configstrings[CS_MAXCLIENTS], " %i ", &i);

	if (i > MAX_CLIENTS)
	{
		return MAX_CLIENTS;
	}

	return i <= 0 ? 1 : i;
}

/* OpenMerc (I6h): demo recording and playback are in cl_demo.c (SPEC-311) */

static void
CL_Setenv_f(void)
{
	int argc = Cmd_Argc();

	if (argc > 2)
	{
		char buffer[1000];
		int i;

		Q_strlcpy(buffer, Cmd_Argv(1), sizeof(buffer));
		Q_strlcat(buffer, "=", sizeof(buffer));

		for (i = 2; i < argc; i++)
		{
			Q_strlcat(buffer, Cmd_Argv(i), sizeof(buffer));
			Q_strlcat(buffer, " ", sizeof(buffer));
		}

		if (putenv(buffer))
		{
			Com_Printf("%s: putenv is failed: %s\n",
				__func__, strerror(errno));
		}
	}

	else if (argc == 2)
	{
		char *env = getenv(Cmd_Argv(1));

		if (env)
		{
			Com_Printf("%s=%s\n", Cmd_Argv(1), env);
		}

		else
		{
			Com_Printf("%s undefined\n", Cmd_Argv(1));
		}
	}
}

static void
CL_Pause_f(void)
{
	/* never pause in multiplayer */
	if ((Cvar_VariableValue("maxclients") > 1) || !Com_ServerState())
	{
		Cvar_SetValue("paused", 0);
		return;
	}

	Cvar_SetValue("paused", !cl_paused->value);
}

void
CL_Quit_f(void)
{
	CL_Disconnect();
	Com_Quit();
}

void
CL_ClearState(void)
{
	S_StopAllSounds();
	CL_ClearEffects();
	CL_ClearTEnts();

	/* wipe the entire cl structure */
	memset(&cl, 0, sizeof(cl));
	CL_ClearEntities();
	CL_Ghoul_ClearState(); /* I1c: client Ghoul system method 8 (SPEC-010) */

	SZ_Clear(&cls.netchan.message);
}

/*
 * Handle a reply from a ping
 */
void
CL_ParseStatusMessage(void)
{
	char *s;

	s = MSG_ReadString(&net_message);

	Com_Printf("%s\n", s);
	M_AddToServerList(net_from, s);
}

/*
 * Load or download any custom player skins and models
 */
static void
CL_Skins_f(void)
{
	int i;

	for (i = 0; i < MAX_CLIENTS; i++)
	{
		if (!cl.configstrings[CS_PLAYERSKINS + i][0])
		{
			continue;
		}

		Com_Printf("client %i: %s\n", i, cl.configstrings[CS_PLAYERSKINS + i]);

		SCR_UpdateScreen();

		IN_Update();  /* pump message loop */

		CL_ParseClientinfo(i);
	}
}

/* This fixes some problems with wrong tagged models and skins */
void
CL_FixUpGender(void)
{
	char *p;
	char sk[80];

	if (gender_auto->value)
	{
		if (gender->modified)
		{
			/* was set directly, don't override the user */
			gender->modified = false;
			return;
		}

		Q_strlcpy(sk, skin->string, sizeof(sk));

		if ((p = strchr(sk, '/')) != NULL)
		{
			*p = 0;
		}

		if ((Q_stricmp(sk, "male") == 0) || (Q_stricmp(sk, "cyborg") == 0))
		{
			Cvar_Set("gender", "male");
		}
		else if ((Q_stricmp(sk, "female") == 0) || (Q_stricmp(sk, "crackhor") == 0))
		{
			Cvar_Set("gender", "female");
		}
		else
		{
			Cvar_Set("gender", "none");
		}

		gender->modified = false;
	}
}

static void
CL_Userinfo_f(void)
{
	Com_Printf("User info settings:\n");
	Info_Print(Cvar_Userinfo());
}

/*
 * Restart the sound subsystem so it can pick up
 * new parameters and flush all sounds
 */
void
CL_Snd_Restart_f(void)
{
	S_Shutdown();
	S_Init();

	CL_RegisterSounds();
}

int precache_check;
int precache_spawncount;
int precache_tex;
int precache_model_skin;
byte *precache_model;

void CL_ResetPrecacheCheck (void)
{
	precache_check = CS_MODELS;
	precache_model = 0;
	precache_model_skin = 0;
}

/*
 * The server will send this command right
 * before allowing the client into the server
 */
static void
CL_Precache_f(void)
{
	/* Yet another hack to let old demos work */
	if (Cmd_Argc() < 2)
	{
		unsigned map_checksum;    /* for detecting cheater maps */

		CM_LoadMap(cl.configstrings[CS_MODELS + 1], true, &map_checksum);
		CL_RegisterSounds();
		CL_PrepRefresh();
		return;
	}

	/* OpenMerc: SoF servers expect "sv_precache <sc>" after loading */
	if (CL_SoF_Active())
	{
		CL_SoF_Precache((int)strtol(Cmd_Argv(1), (char **)NULL, 10));
		return;
	}

	precache_check = CS_MODELS;

	precache_spawncount = (int)strtol(Cmd_Argv(1), (char **)NULL, 10);
	precache_model = 0;
	precache_model_skin = 0;

	CL_RequestNextDownload();
}

static void
CL_CurrentMap_f(void)
{
	Com_Printf("%s\n", cl.configstrings[CS_MODELS + 1]);
}

void CL_WeapLast_f(void); /* cl_network.c */

static void
CL_InitLocal(void)
{
	cls.state = ca_disconnected;
	cls.realtime = Sys_Milliseconds();

	CL_InitInput();
	CL_SoF_Init(); /* OpenMerc */
	CLFX_Init(); /* OpenMerc (I5): effect cvars and commands */

	/* register our variables */
	cin_force43 = Cvar_Get("cin_force43", "1", CVAR_ARCHIVE);

	cl_add_blend = Cvar_Get("cl_blend", "1", 0);
	cl_add_lights = Cvar_Get("cl_lights", "1", 0);
	cl_add_particles = Cvar_Get("cl_particles", "1", 0);
	cl_add_entities = Cvar_Get("cl_entities", "1", 0);
	cl_kickangles = Cvar_Get("cl_kickangles", "1", 0);
	cl_gun = Cvar_Get("cl_gun", "2", CVAR_ARCHIVE);
	cl_footsteps = Cvar_Get("cl_footsteps", "1", 0);
	cl_noskins = Cvar_Get("cl_noskins", "0", 0);
	cl_predict = Cvar_Get("cl_predict", "1", 0);
	cl_showfps = Cvar_Get("cl_showfps", "0", CVAR_ARCHIVE);
	cl_showspeed = Cvar_Get("cl_showspeed", "0", CVAR_ARCHIVE);
	cl_laseralpha = Cvar_Get("cl_laseralpha", "0.3", 0);
	cl_nodownload_list = Cvar_Get("cl_nodownload_list", "", CVAR_ARCHIVE);

	cl_upspeed = Cvar_Get("cl_upspeed", "200", 0);
	cl_forwardspeed = Cvar_Get("cl_forwardspeed", "200", 0);
	cl_sidespeed = Cvar_Get("cl_sidespeed", "200", 0);
	cl_yawspeed = Cvar_Get("cl_yawspeed", "140", 0);
	cl_pitchspeed = Cvar_Get("cl_pitchspeed", "150", 0);
	cl_anglespeedkey = Cvar_Get("cl_anglespeedkey", "1.5", 0);

	cl_run = Cvar_Get("cl_run", "0", CVAR_ARCHIVE);

	cl_shownet = Cvar_Get("cl_shownet", "0", 0);
	cl_showmiss = Cvar_Get("cl_showmiss", "0", 0);
	cl_showclamp = Cvar_Get("showclamp", "0", 0);
	cl_timeout = Cvar_Get("cl_timeout", "120", 0);
	cl_paused = Cvar_Get("paused", "0", 0);
	/* OpenMerc (I1b): default 0 - SoF does not hold the single-player game while the
	   local client loads (the original's game time stays (frame + 2) x 0.1 from the
	   level start; with Yamagi's 1 kos1 lagged 2.1 s and its intro started late) */
	cl_loadpaused = Cvar_Get("cl_loadpaused", "0", CVAR_ARCHIVE);
	cl_audiopaused = Cvar_Get("cl_audiopaused", "1", CVAR_ARCHIVE);
	cl_unpaused_scvis = Cvar_Get("cl_unpaused_scvis", "1", CVAR_ARCHIVE);


	rcon_client_password = Cvar_Get("rcon_password", "", 0);
	rcon_address = Cvar_Get("rcon_address", "", 0);

	cl_lightlevel = Cvar_Get("r_lightlevel", "0", 0);
	cl_r1q2_lightstyle = Cvar_Get("cl_r1q2_lightstyle", "1", CVAR_ARCHIVE);
	cl_limitsparksounds = Cvar_Get("cl_limitsparksounds", "0", CVAR_ARCHIVE);

	/* userinfo */
	name = Cvar_Get("name", "unnamed", CVAR_USERINFO | CVAR_ARCHIVE);
	skin = Cvar_Get("skin", "male/grunt", CVAR_USERINFO | CVAR_ARCHIVE);
	rate = Cvar_Get("rate", "8000", CVAR_USERINFO | CVAR_ARCHIVE);
	hand = Cvar_Get("hand", "0", CVAR_USERINFO | CVAR_ARCHIVE);
	fov = Cvar_Get("fov", "90", CVAR_USERINFO | CVAR_ARCHIVE);
	horplus = Cvar_Get("horplus", "1", CVAR_ARCHIVE);
	windowed_mouse = Cvar_Get("windowed_mouse", "1", CVAR_USERINFO | CVAR_ARCHIVE);
	gender = Cvar_Get("gender", "male", CVAR_USERINFO | CVAR_ARCHIVE);
	gender_auto = Cvar_Get("gender_auto", "1", CVAR_ARCHIVE);
	gender->modified = false;

	// USERINFO cvars are special, they just need to be registered
	Cvar_Get("password", "", CVAR_USERINFO);
	Cvar_Get("spectator", "0", CVAR_USERINFO);

	cl_vwep = Cvar_Get("cl_vwep", "1", CVAR_ARCHIVE);

#ifdef USE_CURL
	cl_http_verifypeer = Cvar_Get("cl_http_verifypeer", "1", CVAR_ARCHIVE);
	cl_http_proxy = Cvar_Get("cl_http_proxy", "", CVAR_ARCHIVE);
	cl_http_filelists = Cvar_Get("cl_http_filelists", "1", 0);
	cl_http_downloads = Cvar_Get("cl_http_downloads", "1", CVAR_ARCHIVE);
	cl_http_max_connections = Cvar_Get("cl_http_max_connections", "4", 0);
	cl_http_show_dw_progress = Cvar_Get("cl_http_show_dw_progress", "0", 0);
	cl_http_bw_limit_rate = Cvar_Get("cl_http_bw_limit_rate", "0", 0);
	cl_http_bw_limit_tmout = Cvar_Get("cl_http_bw_limit_tmout", "0", 0);
#endif

	/* register our commands */
	Cmd_AddCommand("cmd", CL_ForwardToServer_f);
	Cmd_AddCommand("pause", CL_Pause_f);
	Cmd_AddCommand("pingservers", CL_PingServers_f);
	Cmd_AddCommand("skins", CL_Skins_f);

	Cmd_AddCommand("userinfo", CL_Userinfo_f);
	Cmd_AddCommand("snd_restart", CL_Snd_Restart_f);

	Cmd_AddCommand("changing", CL_Changing_f);
	Cmd_AddCommand("disconnect", CL_Disconnect_f);
	CL_Demo_Init(); /* OpenMerc (I6h): record, stop, demomap */

	Cmd_AddCommand("quit", CL_Quit_f);

	Cmd_AddCommand("connect", CL_Connect_f);
	Cmd_AddCommand("reconnect", CL_Reconnect_f);

	Cmd_AddCommand("rcon", CL_Rcon_f);
	Cmd_AddCommand("packet", CL_Packet_f);

	Cmd_AddCommand("setenv", CL_Setenv_f);

	Cmd_AddCommand("precache", CL_Precache_f);

	Cmd_AddCommand("download", CL_Download_f);

	Cmd_AddCommand("currentmap", CL_CurrentMap_f);

	/* forward to server commands
	 * the only thing this does is allow command completion
	 * to work -- all unknown commands are automatically
	 * forwarded to the server */
	Cmd_AddCommand("wave", NULL);
	Cmd_AddCommand("inven", NULL);
	Cmd_AddCommand("kill", NULL);
	Cmd_AddCommand("use", NULL);
	Cmd_AddCommand("drop", NULL);
	Cmd_AddCommand("say", NULL);
	Cmd_AddCommand("say_team", NULL);
	Cmd_AddCommand("info", NULL);
	Cmd_AddCommand("prog", NULL);
	Cmd_AddCommand("give", NULL);
	Cmd_AddCommand("god", NULL);
	Cmd_AddCommand("notarget", NULL);
	Cmd_AddCommand("noclip", NULL);
	Cmd_AddCommand("invuse", NULL);
	Cmd_AddCommand("invprev", NULL);
	Cmd_AddCommand("invnext", NULL);
	Cmd_AddCommand("invdrop", NULL);
	Cmd_AddCommand("weapnext", NULL);
	Cmd_AddCommand("weapprev", NULL);
	Cmd_AddCommand("listentities", NULL);
	Cmd_AddCommand("teleport", NULL);
	Cmd_AddCommand("spawnentity", NULL);
	Cmd_AddCommand("spawnonstart", NULL);
	Cmd_AddCommand("cycleweap", NULL);
	Cmd_AddCommand("weaplast", CL_WeapLast_f);
}

/*
 * Writes key bindings and archived cvars to config.cfg
 */
void
CL_WriteConfiguration(void)
{
	if (cls.state == ca_uninitialized)
	{
		return;
	}

	/* OpenMerc (I1a): <user>/config.cfg in the SoF format (SPEC-301 B11) */
	Com_WriteConfig(false);
}

typedef struct
{
	char *name;
	char *value;
	cvar_t *var;
} cheatvar_t;

/* OpenMerc (I6h): the SoF 1.07f list (SPEC-310 section 14) */
cheatvar_t cheatvars[] = {
	{"timescale", "1"},
	{"timedemo", "0"},
	{"freezeworld", "0"},
	{"r_drawworld", "1"},
	{"cl_testlights", "0"},
	{"r_fullbright", "0"},
	{"gl_drawflat", "0"},
	{"paused", "0"},
	{"fixedtime", "0"},
	{"gl_showtris", "0"},
	{"gl_drawmode", "0"},
	{"gl_fogcolor", "-1"},
	{NULL, NULL}
};

static int numcheatvars;

static void
CL_FixCvarCheats(void)
{
	int i;
	cheatvar_t *var;

	if (!strcmp(cl.configstrings[CS_MAXCLIENTS], "1") ||
		!cl.configstrings[CS_MAXCLIENTS][0])
	{
		return; /* single player can cheat  */
	}

	/* find all the cvars if we haven't done it yet */
	if (!numcheatvars)
	{
		while (cheatvars[numcheatvars].name)
		{
			cheatvars[numcheatvars].var = Cvar_Get(cheatvars[numcheatvars].name,
					cheatvars[numcheatvars].value, 0);
			numcheatvars++;
		}
	}

	/* make sure they are all set to the proper values */
	for (i = 0, var = cheatvars; i < numcheatvars; i++, var++)
	{
		if (strcmp(var->var->string, var->value))
		{
			Cvar_Set(var->name, var->value);
		}
	}
}

static void
CL_UpdateWindowedMouse(void)
{
	if (cls.disable_screen)
	{
		return;
	}

	if (cls.key_dest == key_menu || cls.key_dest == key_console ||
		(cls.key_dest == key_game && (cls.state != ca_active || !cl.refresh_prepped)))
	{
		if (windowed_mouse->value)
		{
			Cvar_SetValue("windowed_mouse", 0);
		}
	}
	else
	{
		if (!windowed_mouse->value)
		{
			Cvar_SetValue("windowed_mouse", 1);
		}
	}
}

void
CL_Frame(int packetdelta, int renderdelta, int timedelta, qboolean packetframe, qboolean renderframe)
{
	static int lasttimecalled;

	// Dedicated?
	if (dedicated->value)
	{
		return;
	}

	// Calculate simulation time.
	cls.nframetime = packetdelta / 1000000.0f;
	cls.rframetime = renderdelta / 1000000.0f;
	cls.realtime = curtime;
	cl.time += timedelta / 1000;

	// Don't extrapolate too far ahead.
	if (cls.nframetime > 0.5f)
	{
		cls.nframetime = 0.5f;
	}

	if (cls.rframetime > 0.5f)
	{
		cls.rframetime = 0.5f;
	}

	// if in the debugger last frame, don't timeout.
	if (timedelta > 5000000)
	{
		cls.netchan.last_received = Sys_Milliseconds();
	}

	// Reset power shield / power screen sound counter.
	num_power_sounds = 0;

	if (!cl_timedemo->value)
	{
		// Don't throttle too much when connecting / loading.
		if ((cls.state == ca_connected) && (packetdelta > 100000))
		{
			packetframe = true;
		}
	}

	// Run HTTP downloads more often while connecting.
#ifdef USE_CURL
	if (cls.state == ca_connected)
	{
		CL_RunHTTPDownloads();
	}
#endif

	// Update input stuff.
	if (packetframe || renderframe)
	{
		/* I1d: context flag -1 at the start of the client frame; after the packets the view weapon's notes
		   (SPEC-003) */
		CL_Player_FrameStart();
		CL_ReadPackets();
		CL_Player_Frame();
		CL_UpdateWindowedMouse();
		IN_Update();
		Cbuf_Execute();
		CL_FixCvarCheats();

		if (cls.state > ca_connecting)
		{
			CL_RefreshCmd();
		}
		else
		{
			CL_RefreshMove();
		}
	}

	if (cls.forcePacket || userinfo_modified)
	{
		packetframe = true;
		cls.forcePacket = false;
	}

	if (packetframe)
	{
		CL_SendCmd();
		CL_CheckForResend();

		// Run HTTP downloads during game.
#ifdef USE_CURL
		CL_RunHTTPDownloads();
#endif
	}

	if (renderframe)
	{
		VID_CheckChanges();
		CL_PredictMovement();

		if (!cl.refresh_prepped && (cls.state == ca_active))
		{
			CL_PrepRefresh();
		}

		/* update the screen */
		if (host_speeds->value)
		{
			time_before_ref = Sys_Milliseconds();
		}

		CL_Ghoul_RunFrame(renderdelta / 1000000.0f); /* I1c: game clock, unclamped frame time (AMEND-006 I1c.1) */
		SCR_UpdateScreen();
		CL_OmRec_Frame(); /* om_autorecord (cl_omrec.c) */

		if (host_speeds->value)
		{
			time_after_ref = Sys_Milliseconds();
		}

		/* update audio */
		S_Update(cl.refdef.vieworg, cl.v_forward, cl.v_right, cl.v_up);

		/* advance local effects for next frame */
		CL_RunDLights();
		CL_RunLightStyles();
		SCR_RunCinematic();
		SCR_RunConsole();

		/* Update framecounter */
		cls.framecount++;

		if (log_stats->value)
		{
			if (cls.state == ca_active)
			{
				if (!lasttimecalled)
				{
					lasttimecalled = Sys_Milliseconds();

					if (log_stats_file)
					{
						fprintf(log_stats_file, "0\n");
					}
				}

				else
				{
					int now = Sys_Milliseconds();

					if (log_stats_file)
					{
						fprintf(log_stats_file, "%d\n", now - lasttimecalled);
					}

					lasttimecalled = now;
				}
			}
		}
	}
}

void
CL_Init(void)
{
	if (dedicated->value)
	{
		return; /* nothing running on the client */
	}

	/* all archived variables will now be loaded */
	Con_Init();

	SCR_Init();

	CL_Ghoul_Init(); /* I1c: the client Ghoul system exists before the renderer asks for it */
	CL_Player_Init(); /* I1d: client side of the player module (SPEC-003) */

	VID_Init();

	/* OpenMerc (I1a): SoF order - video, then the sound module, which
	   needs the window (SPEC-301 Behaviour 1 step 14) */
	S_Init();

	IN_Init();

	V_Init();

	net_message.data = net_message_buffer;

	net_message.maxsize = sizeof(net_message_buffer);

	M_Init();

#ifdef USE_CURL
	CL_InitHTTPDownloads();
#endif

	cls.disable_screen = true; /* don't draw yet */

	CL_InitLocal();
	CL_OmRec_Init(); /* om_autorecord (cl_omrec.c) */

	Cbuf_Execute();

	Key_ReadConsoleHistory();
}

void
CL_Shutdown(void)
{
	static qboolean isdown = false;

	if (isdown)
	{
		printf("recursive shutdown\n");
		return;
	}

	isdown = true;

	CL_OmRec_Shutdown(); /* om_autorecord: demo end marker, log closed */

#ifdef USE_CURL
	CL_HTTP_Cleanup(true);
#endif

	CL_WriteConfiguration();

	Key_WriteConsoleHistory();

	SNDH_SetShuttingDown(true); /* OpenMerc (I1a): sound struct #3 +0x24 */
	S_Shutdown();
	IN_Shutdown();
	VID_Shutdown();

	CL_ClearEntities();
	M_Free();
	CL_Player_Shutdown(); /* I1d: before the Ghoul objects go */
	CL_Ghoul_Shutdown(); /* I1c: after the menus freed their models (process exit, SPEC-010) */
}
