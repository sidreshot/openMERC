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
 * Platform independent initialization, main loop and frame handling.
 *
 * =======================================================================
 *
 * OpenMerc: runs the Internet server browser (client) and the lobby chat,
 * and sends the last GameSpy heartbeat at shutdown.
 * Implements: SPEC-204
 *
 * OpenMerc: the Soldier of Fortune 1.07f start sequence - two +set passes
 * around the filesystem start, default.cfg and config.cfg, the SoF engine
 * cvars, CPU report, string packages, parental locks, the autoexec.cfg
 * rule, "start" / "dedicated_start" unless + commands or -nostart, the SoF
 * banner and version.
 * Implements: SPEC-301 (Behaviour 1, 2, 3, 7), SPEC-302
 *
 * =======================================================================
 */

#include "header/common.h"
#include "header/om_version.h"
#include "header/gamespy.h"
#include "header/gamespy_chat.h" /* OpenMerc (I2d) */
#ifndef DEDICATED_ONLY
#include "../client/header/serverlist.h"
#endif
#include <setjmp.h>

cvar_t *developer;
cvar_t *modder;
cvar_t *timescale;
cvar_t *fixedtime;
cvar_t *cl_maxfps;
cvar_t *dedicated;

extern cvar_t *color_terminal;
extern cvar_t *logfile_active;
extern jmp_buf abortframe; /* an ERR_DROP occured, exit the entire frame */

#ifndef DEDICATED_ONLY
FILE *log_stats_file;
cvar_t *busywait;
cvar_t *cl_async;
cvar_t *cl_timedemo;
cvar_t *vid_maxfps;
cvar_t *host_speeds;
cvar_t *log_stats;
cvar_t *showtrace;
#endif

// Forward declarations
#ifndef DEDICATED_ONLY
float GLimp_GetRefreshRate(void);
qboolean R_IsVSyncActive(void);
#endif

/* host_speeds times */
int time_before_game;
int time_after_game;
int time_before_ref;
int time_after_ref;

// Used in the network- and input paths.
int curtime;

#ifndef DEDICATED_ONLY
void Key_Init(void);
void Key_Shutdown(void);
void SCR_EndLoadingPlaque(void);
#endif

// Is the game portable?
qboolean is_portable;

// Game given by user
char userGivenGame[MAX_QPATH];

// Game should quit next frame.
// Hack for the signal handlers.
qboolean quitnextframe;

#ifndef DEDICATED_ONLY
#ifdef SDL_CPUPauseInstruction
#  define Sys_CpuPause() SDL_CPUPauseInstruction()
#else
static YQ2_ATTR_INLINE void Sys_CpuPause(void)
{
#if defined(__GNUC__)
#if (__i386 || __x86_64__)
	asm volatile("pause");
#elif defined(__aarch64__) || (defined(__ARM_ARCH) && __ARM_ARCH >= 7) || defined(__ARM_ARCH_6K__)
	asm volatile("yield");
#elif defined(__powerpc__) || defined(__powerpc64__)
	asm volatile("or 27,27,27");
#elif defined(__riscv) && __riscv_xlen == 64
	asm volatile(".insn i 0x0F, 0, x0, x0, 0x010");
#endif
#elif defined(_MSC_VER)
#if defined(_M_IX86) || defined(_M_X64)
	_mm_pause();
#elif defined(_M_ARM) || defined(_M_ARM64)
	__yield();
#endif
#endif
}
#endif
#endif

static void Qcommon_Frame(int usec);

// ----

static void
Qcommon_Buildstring(void)
{
	int i;
	size_t verLen;
	const char* versionString;


	versionString = va("%s", OM_BUILD_STRING);
	verLen = strlen(versionString);

	printf("\n%s\n", versionString);

	for( i = 0; i < verLen; ++i)
	{
		printf("=");
	}

	printf("\n");


#ifndef DEDICATED_ONLY
	printf("Client build options:\n");

#ifdef USE_CURL
	printf(" + cURL HTTP downloads\n");
#else
	printf(" - cURL HTTP downloads\n");
#endif

	printf(" + OMsnd (OpenAL Soft) audio module\n");

#ifdef SYSTEMWIDE
	printf(" + Systemwide installation\n");
#else
	printf(" - Systemwide installation\n");
#endif
#endif

#ifdef USE_XDG
	printf(" + XDG directory support\n");
#else
	printf(" - XDG directory support\n");
#endif

	printf("Platform: %s\n", YQ2OSTYPE);
	printf("Architecture: %s\n", YQ2ARCH);
}

static void
Qcommon_Mainloop(void)
{
	long long newtime;
	long long oldtime = Sys_Microseconds();

	/* The mainloop. The legend. */
	while (1)
	{
#ifndef DEDICATED_ONLY
		if (!cl_timedemo->value)
		{
			// Throttle the game a little bit.
			if (busywait->value)
			{
				long long spintime = Sys_Microseconds();

				while (1)
				{
					/* Give the CPU a hint that this is a very tight
					   spinloop. One PAUSE instruction each loop is
					   enough to reduce power consumption and head
					   dispersion a lot, it's 95°C against 67°C on
					   a Kaby Lake laptop. */
					Sys_CpuPause();

					if (Sys_Microseconds() - spintime >= 5)
					{
						break;
					}
				}
			}
			else
			{
				Sys_Nanosleep(5000);
			}
		}
#else
		Sys_Nanosleep(850000);
#endif

		newtime = Sys_Microseconds();

		// Save global time for network- and input code.
		curtime = (int)(newtime / 1000ll);

		Qcommon_Frame(newtime - oldtime);
		oldtime = newtime;
	}
}

/* SoF: default.cfg, then config.cfg (SPEC-301 Behaviour 2) */
void Qcommon_ExecConfigs(qboolean gameStartUp)
{
	Cbuf_AddText("exec default.cfg\n");
	Cbuf_AddText("exec config.cfg\n");

	if (gameStartUp)
	{
		/* second +set pass: the command line overrides saved settings */
		Cbuf_AddEarlyCommands(true);
	}

	Cbuf_Execute();
}

static qboolean checkForHelp(int argc, char **argv)
{
	const char* helpArgs[] = { "--help", "-h", "-help", "-?", "/?" };
	const int numHelpArgs = ARRLEN(helpArgs);

	for (int i=1; i<argc; ++i)
	{
		const char* arg = argv[i];

		for (int h=0; h<numHelpArgs; ++h)
		{
			if (Q_stricmp(arg, helpArgs[h]) == 0)
			{
				printf("%s\n", OM_BUILD_STRING);
				printf("Most interesting commandline arguments:\n");
				printf("-h or --help: Show this help\n");
				printf("-basedir <path>\n");
				printf("  the directory that contains base/ (default: the working directory)\n");
				printf("-user <path>\n");
				printf("  the user directory: configs, saves, sof.log (default ./user)\n");
				printf("-game <mod>, -cddir <path>\n");
				printf("-portable\n");
				printf("  keep the engine's own files (console history, stdout.txt) in the binary directory\n");
				printf("-nostart\n");
				printf("  do not run the 'start' alias after initialisation\n");
				printf("+set <cvarname> <value>\n");
				printf("  Set the given cvar to the given value, e.g. +set vid_fullscreen 0\n");
				printf("+<command>\n");
				printf("  run a console command after initialisation, e.g. +map kos1\n");

				return true;
			}
		}
	}

	return false;
}

/* text of the error that ended initialisation (set by Com_Error) */
extern char com_errortext[];

static cvar_t *host_speeds_cvar;
static cvar_t *log_stats_cvar;
static cvar_t *showtrace_cvar;

void
Qcommon_Init(int argc, char **argv)
{
	// Jump point used in emergency situations.
	if (YQ2_SETJMP(abortframe))
	{
		Sys_Error("Error during initialization: %s", com_errortext);
	}

	if (checkForHelp(argc, argv))
	{
		// ok, --help or similar commandline option was given
		// and info was printed, exit the game now
		exit(1);
	}

	// Print the build and version string
	Qcommon_Buildstring();

	// Seed PRNG
	randk_seed();
	SoF_SeedRandom((unsigned)Sys_Milliseconds());

	// Initialize zone malloc().
	Z_Init();

	/* 1.-2. command line, command buffer, commands, cvars, keys */
	COM_InitArgv(argc, argv);
	Swap_Init();
	Cbuf_Init();
	Cmd_Init();
	Cvar_Init();

#ifndef DEDICATED_ONLY
	Key_Init();
#else
	SoF_InitDedicatedKeys();
#endif

	/* 3. first +set pass: before the filesystem starts, so that
	   +set game/user/basedir take effect */
	Cbuf_AddEarlyCommands(false);
	Cbuf_Execute();

	// remember the initial game name that might have been set on commandline
	{
		cvar_t* gameCvar = Cvar_Get("game", "", CVAR_LATCH | CVAR_SERVERINFO);
		const char* game = "";

		if(gameCvar->string && gameCvar->string[0])
		{
			game = gameCvar->string;
		}

		Q_strlcpy(userGivenGame, game, sizeof(userGivenGame));
	}

	/* 4. filesystem */
	FS_InitFilesystem();

	/* 5.-6. default.cfg, config.cfg, second +set pass */
	Qcommon_ExecConfigs(true);

	/* the SoF engine cvars, with their specified defaults and flags
	   (values set by the configs are kept) */
#ifdef DEDICATED_ONLY
	SoF_RegisterEngineCvars(true);
#else
	SoF_RegisterEngineCvars(Cvar_VariableValue("dedicated") != 0);
#endif

	/* 7. common commands and cvars */
	Cmd_AddCommand("z_stats", Z_Stats_f);
	Cmd_AddCommand("error", Com_Error_f);
	Cmd_AddCommand("subliminal", Com_Subliminal_f);

	host_speeds_cvar = Cvar_Get("host_speeds", "0", 0);
	log_stats_cvar = Cvar_Get("log_stats", "0", 0);
	developer = Cvar_Get("developer", "0", 0);
	timescale = Cvar_Get("timescale", "1", 0);
	fixedtime = Cvar_Get("fixedtime", "0", 0);
	logfile_active = Cvar_Get("logfile", "0", 0);
	showtrace_cvar = Cvar_Get("showtrace", "0", 0);
#ifndef DEDICATED_ONLY
	dedicated = Cvar_Get("dedicated", "0", CVAR_NOSET);
#else
	dedicated = Cvar_Get("dedicated", "1", CVAR_NOSET);
#endif
	Cvar_Get("version", SOF_VERSION_STRING, CVAR_SERVERINFO | CVAR_NOSET);

#ifndef DEDICATED_ONLY
	/* a client cvar of SoF; the client frame loop also needs it when the
	   client executable runs as a dedicated server */
	cl_maxfps = Cvar_Get("cl_maxfps", "30", 0);
#endif
	/* Yamagi's colorterminal and modder cvars are not created (SoF has
	   neither); Com_MDPrintf then follows developer only */

#ifndef DEDICATED_ONLY
	busywait = Cvar_Get("busywait", "1", CVAR_ARCHIVE);
	cl_async = Cvar_Get("cl_async", "1", CVAR_ARCHIVE);
	cl_timedemo = Cvar_Get("timedemo", "0", 0);
	/* OpenMerc V1: 0 = the display refresh rate (Yamagi: 300, see docs/DEVIATIONS.md) */
	vid_maxfps = Cvar_Get("vid_maxfps", "0", CVAR_ARCHIVE);
	host_speeds = host_speeds_cvar;
	log_stats = log_stats_cvar;
	showtrace = showtrace_cvar;
#endif

	if (developer->value)
	{
		/* SoF shows a "Developer Mode" message box here; OpenMerc prints
		   the warning instead so that unattended runs never block */
		Com_Printf("Developer Mode: this mode is intended for customer support.\n");
	}

	// We can't use the clients "quit" command when running dedicated.
	if (dedicated->value)
	{
		Cmd_AddCommand("quit", Com_Quit);
	}

	/* 8. system layer, CPU report */
	Sys_Init();
	SoF_CpuInfo(true);
	SoF_InitSystemCommands();

	/* 9. string packages */
	SP_Init();

	/* 10. networking */
	NET_Init();
	SoF_PrintLocalAddresses();
	Netchan_Init();

	/* 11. violence settings (no parental locks: sof_locks.c) */
	SoF_InitLocks();

	/* 13. server */
	SV_Init();
	SoF_InitAdminCommands();
	SG_InitCommands();
	GSC_Init(); /* OpenMerc (I2d): GameSpy lobby chat (SPEC-204 section 5) */

#ifndef DEDICATED_ONLY
	/* 14. client: console, video, sound, input; then autoexec.cfg */
	if (!dedicated->value)
	{
		CL_Init();
		CL_ServerList_Init();
		SoF_InitClientCommands();

		/* the engine-owned client packages (SPEC-313 "Package owners") */
		SP_Register("items", SP_OWNER_ENGINE);
		SP_Register("weapons", SP_OWNER_ENGINE);
		SP_Register("general", SP_OWNER_ENGINE);
		SP_Register("dm_generic", SP_OWNER_ENGINE);

		Com_ExecAutoexec();
		Cbuf_Execute();
	}
#endif

	SoF_RegisterClientForwardCommands();

	/* a later registration may have added flags; the specified ones win */
	SoF_EnforceCvarFlags();

#ifndef DEDICATED_ONLY
	/* Quake II kept the screen disabled here until the "d1" demo map ended the
	   plaque; SoF's default start opens a menu (no level), and -nostart shows the
	   console (SPEC-301 Behaviour 7 / vector 4), so the screen is enabled in
	   every case, and BEFORE `start` runs: ending the plaque closes the menus
	   (SPEC-310 9), which would shut the disclaimer the start just opened.
	   Without this a plain start stayed black for the 120 s plaque timeout.
	   + commands only enter the buffer here and run on the first frame. */
	SCR_EndLoadingPlaque();
#endif

	/* 15. + commands from the command line, else the default start */
	if (!Cbuf_AddLateCommands())
	{
		if (!COM_CheckParm("-nostart"))
		{
			if (!dedicated->value)
			{
				Cbuf_AddText("start\n");
			}
			else
			{
				Cbuf_AddText("dedicated_start\n");
			}
		}

		Cbuf_Execute();
	}

	Com_Printf("====== Soldier of Fortune Initialized ======\n\n");

	// Call the main loop
	Qcommon_Mainloop();
}

#ifndef DEDICATED_ONLY
static void
Qcommon_Frame(int usec)
{
#ifdef _WIN32
	Sys_Crash_Frame(); /* OpenMerc: keep the crash report filter */
#endif

	// Used for the dedicated server console.
	char *s;

	// Statistics.
	int time_before = 0;
	int time_between = 0;
	int time_after;

	// Target packetframerate.
	float pfps;

	// Target renderframerate.
	float rfps;

	// Time since last packetframe in microsec.
	static int packetdelta = 1000000;

	// Time since last renderframe in microsec.
	static int renderdelta = 1000000;

	// Accumulated time since last client run.
	static int clienttimedelta = 0;

	// Accumulated time since last server run.
	static int servertimedelta = 0;

	/* A packetframe runs the server and the client,
	   but not the renderer. The minimal interval of
	   packetframes is about 10.000 microsec. If run
	   more often the movement prediction in pmove.c
	   breaks. That's the Q2 variant if the famous
	   125hz bug. */
	qboolean packetframe = true;

	/* A rendererframe runs the renderer, but not the
	   client or the server. The minimal interval is
	   about 1000 microseconds. */
	qboolean renderframe = true;


	/* Tells the client to shutdown.
	   Used by the signal handlers. */
	if (quitnextframe)
	{
		/* once, and as a complete line: the command buffer only runs
		   newline-terminated lines */
		quitnextframe = false;
		Cbuf_AddText("quit\n");
	}


	/* In case of ERR_DROP we're jumping here. Don't know
	   if that's really save but it seems to work. So leave
	   it alone. */
	if (YQ2_SETJMP(abortframe))
	{
		return;
	}


	if (log_stats->modified)
	{
		log_stats->modified = false;

		if (log_stats->value)
		{
			if (log_stats_file)
			{
				fclose(log_stats_file);
				log_stats_file = 0;
			}

			log_stats_file = Q_fopen("stats.log", "w");

			if (log_stats_file)
			{
				fprintf(log_stats_file, "entities,dlights,parts,frame time\n");
			}
		}
		else
		{
			if (log_stats_file)
			{
				fclose(log_stats_file);
				log_stats_file = 0;
			}
		}
	}


	// Timing debug crap. Just for historical reasons.
	if (fixedtime->value)
	{
		usec = (int)fixedtime->value;
	}
	else if (timescale->value)
	{
		usec *= timescale->value;
	}


	if (showtrace->value)
	{
		extern int c_traces, c_brush_traces;
		extern int c_pointcontents;

		Com_Printf("%4i traces  %4i points\n", c_traces, c_pointcontents);
		c_traces = 0;
		c_brush_traces = 0;
		c_pointcontents = 0;
	}


	/* We can render 1000 frames at maximum, because the minimum
	   frametime of the client is 1 millisecond. And of course we
	   need to render something, the framerate can never be less
	   then 1. Cap vid_maxfps between 1 and 999. */
	if (vid_maxfps->value > 999)
	{
		Cvar_SetValue("vid_maxfps", 999);
	}

	/* OpenMerc V1: vid_maxfps 0 (the default) means the display refresh rate;
	   165 when the display does not report one */
	float maxfps = vid_maxfps->value;

	if (maxfps < 1)
	{
		float rr = GLimp_GetRefreshRate();

		maxfps = (rr >= 1) ? rr : 165;
	}

	if (cl_maxfps->value > 250)
	{
		Cvar_SetValue("cl_maxfps", 250);
	}

	// Calculate target and renderframerate.
	if (R_IsVSyncActive())
	{
		float refreshrate = GLimp_GetRefreshRate();

		// using refreshRate - 2, because targeting a value slightly below the
		// (possibly not 100% correctly reported) refreshRate would introduce jittering, so only
		// use vid_maxfps if it looks like the user really means it to be different from refreshRate
		if (maxfps < refreshrate - 2 )
		{
			rfps = maxfps;
			// we can't have more packet frames than render frames, so limit pfps to rfps
			pfps = (cl_maxfps->value > rfps) ? rfps : cl_maxfps->value;
		}
		else // target refresh rate, not vid_maxfps
		{
			/* if vsync is active, we increase the target framerate a bit for two reasons
			   1. On Windows, GLimp_GetFrefreshRate() (or the SDL counterpart, or even
			      the underlying WinAPI function) often returns a too small value,
			      like 58 or 59 when it's really 59.95 and thus (as integer) should be 60
			   2. vsync will throttle us to refreshrate anyway, so there is no harm
			      in starting the frame *a bit* earlier, instead of risking starting
			      it too late */
			rfps = refreshrate * 1.2f;
			// we can't have more packet frames than render frames, so limit pfps to rfps
			// but in this case use tolerance for comparison and assign rfps with tolerance
			pfps = (cl_maxfps->value < refreshrate - 2) ? cl_maxfps->value : rfps;
		}
	}
	else
	{
		rfps = maxfps;
		// we can't have more packet frames than render frames, so limit pfps to rfps
		pfps = (cl_maxfps->value > rfps) ? rfps : cl_maxfps->value;
	}

	// cl_maxfps <= 0 means: automatically choose a packet framerate that should work
	// well with the render framerate, which is the case if rfps is a multiple of pfps
	if (cl_maxfps->value <= 0.0f && cl_async->value != 0.0f)
	{
		// packet framerates between about 45 and 90 should be ok,
		// with other values the game (esp. movement/clipping) can become glitchy
		// as pfps must be <= rfps, for rfps < 90 just use that as pfps
		if (rfps < 90.0f)
		{
			pfps = rfps;
		}
		else
		{
			/* we want an integer divider, so every div-th renderframe is a packetframe.
			   this formula gives nice dividers that keep pfps as close as possible
			   to 60 (which seems to be ideal):
			   - for < 150 rfps div will be 2, so pfps will be between 45 and ~75
			     => exactly every second renderframe we also run a packetframe
			   - for < 210 rfps div will be 3, so pfps will be between 50 and ~70
			     => exactly every third renderframe we also run a packetframe
			   - etc, the higher the rfps, the closer the pfps-range will be to 60
			     (and you probably get the very best results by running at a
			      render framerate that's a multiple of 60) */
			float div = round(rfps/60);
			pfps = rfps/div;
		}
	}

	// Calculate timings.
	packetdelta += usec;
	renderdelta += usec;
	clienttimedelta += usec;
	servertimedelta += usec;

	if (!cl_timedemo->value)
	{
		if (cl_async->value)
		{
			// Render frames.
			if (renderdelta < (1000000.0f / rfps))
			{
				renderframe = false;
			}

			// Network frames.
			float packettargetdelta = 1000000.0f / pfps;
			// "packetdelta + renderdelta/2 >= packettargetdelta" if now we're
			// closer to when we want to run the next packetframe than we'd
			// (probably) be after the next render frame
			// also, we only run packetframes together with renderframes,
			// because we must have at least one render frame between two packet frames
			// TODO: does it make sense to use the average renderdelta of the last X frames
			//       instead of just the last renderdelta?
			/* OpenMerc ENG2 (SPEC-AMEND-010 M1/M2): with a server in this process the
			   client is not capped by cl_maxfps: every rendered frame is also a packet
			   frame (input, one command, prediction, view advance together) */
			if (!renderframe || (!Com_ServerState() &&
					packetdelta + renderdelta/2 < packettargetdelta))
			{
				packetframe = false;
			}
		}
		else
		{
			// Cap frames at target framerate.
			if (renderdelta < (1000000.0f / rfps)) {
				renderframe = false;
				packetframe = false;
			}
		}
	}

	// Dedicated server terminal console.
	do {
		s = Sys_ConsoleInput();

		if (s) {
			Cbuf_AddText(va("%s\n", s));
		}
	} while (s);

	/* OpenMerc (I1a): the command buffer runs once per (packet) frame,
	   so that "wait" waits one frame as in SoF (SPEC-302 Behaviour 3);
	   Yamagi's loop also passes here between frames */
	SoF_RunDelayed(); /* om_delay test helper */

	if (packetframe)
	{
		Cbuf_Execute();
	}


	if (host_speeds->value)
	{
		time_before = Sys_Milliseconds();
	}


	// Run the serverframe.
	if (packetframe) {
		SV_Frame(servertimedelta);
		servertimedelta = 0;
	}


	if (host_speeds->value)
	{
		time_between = Sys_Milliseconds();
	}


	// Run the client frame.
	if (packetframe || renderframe) {
		CL_Frame(packetdelta, renderdelta, clienttimedelta, packetframe, renderframe);
		CL_ServerList_Frame();
		GSC_Frame(); /* OpenMerc (I2d) */
		clienttimedelta = 0;
	}


	if (host_speeds->value)
	{
		int all, sv, gm, cl, rf;

		time_after = Sys_Milliseconds();
		all = time_after - time_before;
		sv = time_between - time_before;
		cl = time_after - time_between;
		gm = time_after_game - time_before_game;
		rf = time_after_ref - time_before_ref;
		sv -= gm;
		cl -= rf;
		Com_Printf("all:%3i sv:%3i gm:%3i cl:%3i rf:%3i\n", all, sv, gm, cl, rf);
	}


	// Reset deltas and mark frame.
	if (packetframe) {
		packetdelta = 0;
	}

	if (renderframe) {
		renderdelta = 0;
	}
}
#else
static void
Qcommon_Frame(int usec)
{
#ifdef _WIN32
	Sys_Crash_Frame(); /* OpenMerc: keep the crash report filter */
#endif

	// For the dedicated server terminal console.
	char *s;

	// Target packetframerate.
	int pfps;

	// Time since last packetframe in microsec.
	static int packetdelta = 1000000;

	// Accumulated time since last server run.
	static int servertimedelta = 0;

	/* A packetframe runs the server and the client,
	   but not the renderer. The minimal interval of
	   packetframes is about 10.000 microsec. If run
	   more often the movement prediction in pmove.c
	   breaks. That's the Q2 variant if the famous
	   125hz bug. */
	qboolean packetframe = true;


	/* Tells the client to shutdown.
	   Used by the signal handlers. */
	if (quitnextframe)
	{
		/* once, and as a complete line: the command buffer only runs
		   newline-terminated lines */
		quitnextframe = false;
		Cbuf_AddText("quit\n");
	}


	/* In case of ERR_DROP we're jumping here. Don't know
	   if that' really save but it seems to work. So leave
	   it alone. */
	if (YQ2_SETJMP(abortframe))
	{
		return;
	}


	// Timing debug crap. Just for historical reasons.
	if (fixedtime->value)
	{
		usec = (int)fixedtime->value;
	}
	else if (timescale->value)
	{
		usec *= timescale->value;
	}


	// Target framerate. OpenMerc (I1a): a SoF dedicated server has no
	// cl_maxfps; 60 server passes per second as Yamagi's default rate.
	pfps = (cl_maxfps && (cl_maxfps->value > 0)) ? (int)cl_maxfps->value : 60;


	// Calculate timings.
	packetdelta += usec;
	servertimedelta += usec;


	// Network frame time.
	if (packetdelta < (1000000.0f / pfps)) {
		packetframe = false;
	}


	// Dedicated server terminal console.
	do {
		s = Sys_ConsoleInput();

		if (s) {
			Cbuf_AddText(va("%s\n", s));
		}
	} while (s);

	/* OpenMerc (I1a): the command buffer runs once per (packet) frame,
	   so that "wait" waits one frame as in SoF (SPEC-302 Behaviour 3);
	   Yamagi's loop also passes here between frames */
	SoF_RunDelayed(); /* om_delay test helper */

	if (packetframe)
	{
		Cbuf_Execute();
	}


	// Run the serverframe.
	if (packetframe) {
		SV_Frame(servertimedelta);
		servertimedelta = 0;
		GSC_Frame(); /* OpenMerc (I2d) */

		// Reset deltas if necessary.
		packetdelta = 0;
	}
}
#endif

void
Qcommon_Shutdown(void)
{
	/* last GameSpy heartbeat, query socket closed (needs the cvars) */
	SV_GameSpy_Shutdown();
	GSC_Shutdown(); /* OpenMerc (I2d): QUIT to the chat server */

	/* fs_createpak: paklist.txt at program exit (SPEC-300 Behaviour 8) */
	FS_WritePakList();
	SP_Shutdown();

	FS_ShutdownFilesystem();
	Cvar_Fini();

#ifndef DEDICATED_ONLY
	Key_Shutdown();
#endif

	Cmd_Shutdown();
}
