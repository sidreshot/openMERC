/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Soldier of Fortune 1.07f engine core helpers without a Quake II
 * counterpart: developer bits, the shared random generator, the CPU report
 * (cpuid), the system commands (error, subliminal, minimise, winstart,
 * disconnect on a dedicated server, ip_list), the autoexec.cfg rule and
 * the config.cfg / configs/CURRENT.cfg writers with saveconfig / savekeys.
 *
 * Implements: SPEC-301 (Behaviour 1 step 7/8, 4, 11), SPEC-302 (Behaviour 8,
 *             developer), SPEC-313 (§9 random numbers, §12 vid_modes)
 */

#include "header/common.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#define OM_HAVE_CPUID 1
#endif
#else
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#if !defined(_WIN32) && defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
#include <cpuid.h>
#include <x86intrin.h>
#include <unistd.h>
#define OM_HAVE_CPUID 1
#endif

#ifndef DEDICATED_ONLY
#include "../client/header/client.h"
#endif

/* ---------------------------------------------------------------------
 * developer bits: 1 developer prints, 2 system console echo, 4 verbose
 * filesystem and save-chunk traces (SPEC-302)
 * --------------------------------------------------------------------- */

qboolean
Com_DeveloperBit(int bit)
{
	if (!developer)
	{
		return false;
	}

	return ((int)developer->value & bit) ? true : false;
}

/* ---------------------------------------------------------------------
 * random numbers (SPEC-313 §9): one shared 32-bit state
 * --------------------------------------------------------------------- */

static unsigned sof_rand_state;

void
SoF_SeedRandom(unsigned seed)
{
	sof_rand_state = seed;
}

static int
SoF_Rand15(void)
{
	sof_rand_state = sof_rand_state * 214013u + 2531011u;
	return (int)(sof_rand_state >> 17) & 0x7fff;
}

float
flrand(float min, float max)
{
	int r = SoF_Rand15();

	return min + (max - min) * (float)r / 32768.0f;
}

int
irand(int min, int max)
{
	int r = SoF_Rand15();

	return min + (((max - min + 1) * r) >> 15);
}

/* ---------------------------------------------------------------------
 * cpuid (SPEC-302 Behaviour 8)
 * --------------------------------------------------------------------- */

#ifdef OM_HAVE_CPUID
static void
SoF_Cpuid(unsigned leaf, unsigned regs[4])
{
#ifdef _MSC_VER
	int r[4];

	__cpuid(r, (int)leaf);
	regs[0] = r[0];
	regs[1] = r[1];
	regs[2] = r[2];
	regs[3] = r[3];
#else
	__cpuid(leaf, regs[0], regs[1], regs[2], regs[3]);
#endif
}

static unsigned long long
SoF_Rdtsc(void)
{
	return __rdtsc();
}
#endif

/* measured clock in MHz (0 when unknown) */
static int
SoF_MeasureMHz(void)
{
#if defined(OM_HAVE_CPUID) && defined(_WIN32)
	LARGE_INTEGER f, t0, t1;
	unsigned long long c0, c1;

	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&t0);
	c0 = SoF_Rdtsc();

	do
	{
		QueryPerformanceCounter(&t1);
	}
	while ((t1.QuadPart - t0.QuadPart) < f.QuadPart / 20);

	c1 = SoF_Rdtsc();

	return (int)(((double)(c1 - c0) * (double)f.QuadPart /
				(double)(t1.QuadPart - t0.QuadPart)) / 1000000.0 + 0.5);
#elif defined(OM_HAVE_CPUID)
	long long u0 = Sys_Microseconds(), u1;
	unsigned long long c0 = SoF_Rdtsc(), c1;

	do
	{
		u1 = Sys_Microseconds();
	}
	while (u1 - u0 < 50000);

	c1 = SoF_Rdtsc();

	return (int)((double)(c1 - c0) / (double)(u1 - u0) + 0.5);
#else
	return 0;
#endif
}

/* physical memory in MB (a 32-bit process sees at most 2047) */
static int
SoF_PhysicalMB(void)
{
#ifdef _WIN32
	MEMORYSTATUS ms;

	ms.dwLength = sizeof(ms);
	GlobalMemoryStatus(&ms);

	return (int)(ms.dwTotalPhys / (1024 * 1024));
#elif defined(_SC_PHYS_PAGES)
	long long pages = sysconf(_SC_PHYS_PAGES);
	long long size = sysconf(_SC_PAGE_SIZE);

	return (int)((pages * size) / (1024 * 1024));
#else
	return 0;
#endif
}

void
SoF_CpuInfo(qboolean print)
{
	char vendor[13] = "";
	const char *vname;
	const char *type = "Original OEM processor";
	qboolean mmx = false, amd3d = false, rdtsc = false;
	int mhz, speed, memory;

#ifdef OM_HAVE_CPUID
	{
		unsigned r[4];
		unsigned maxext;

		SoF_Cpuid(0, r);
		memcpy(vendor, &r[1], 4);
		memcpy(vendor + 4, &r[3], 4);
		memcpy(vendor + 8, &r[2], 4);
		vendor[12] = 0;

		SoF_Cpuid(1, r);

		switch ((r[0] >> 12) & 3)
		{
			case 1:
				type = "OverDrive processor";
				break;
			case 2:
				type = "Dual processor";
				break;
			case 3:
				type = "Reserved";
				break;
			default:
				break;
		}

		mmx = (r[3] & (1u << 23)) ? true : false;
		rdtsc = (r[3] & (1u << 4)) ? true : false;

		SoF_Cpuid(0x80000000u, r);
		maxext = r[0];

		if (maxext >= 0x80000001u)
		{
			SoF_Cpuid(0x80000001u, r);
			amd3d = (r[3] & (1u << 31)) ? true : false;
		}
	}
#endif

	if (!strcmp(vendor, "AuthenticAMD"))
	{
		vname = "AMD";
	}
	else if (!strcmp(vendor, "GenuineIntel"))
	{
		vname = "Intel";
	}
	else if (!strcmp(vendor, "CyrixInstead"))
	{
		vname = "Cyrix";
	}
	else
	{
		vname = vendor[0] ? vendor : "Unknown";
	}

	mhz = rdtsc ? SoF_MeasureMHz() : 0;

	/* vendor-adjusted clock */
	speed = mhz;

	if (!strcmp(vname, "AMD"))
	{
		speed = mhz * 5 / 6;
	}
	else if (!strcmp(vname, "Cyrix"))
	{
		speed = mhz * 3 / 4;
	}

	memory = SoF_PhysicalMB() + 1;

	if (print)
	{
		Com_Printf("----------- Cpu info -----------\n");
		/* the original adds a model name from its own table; OpenMerc
		   has no such table (open question in OPEN_QUESTIONS.md) */
		Com_Printf("Processor : %s \n", vname);
		Com_Printf("Type      : %s\n", type);
		Com_Printf("Speed     : %i MHz\n", mhz);

		if (mmx)
		{
			Com_Printf("MMX instructions supported\n");
		}

		if (amd3d)
		{
			Com_Printf("3DNow! instructions supported\n");
		}

		if (rdtsc)
		{
			Com_Printf("RDTSC instruction supported\n");
		}

		Com_Printf("--------------------------------\n");
		Com_Printf("%iMB of physical memory\n", memory);
	}

	Cvar_SetValue("cpu_speed", (float)speed);
	Cvar_SetValue("cpu_speed_level", (float)((speed >= 391) ? 4 : ((speed > 290) ? 3 : 2)));
	Cvar_SetValue("cpu_memory", (float)memory);
	Cvar_SetValue("cpu_memory_level", (float)((memory >= 65) ? 3 : ((memory > 48) ? 2 : 1)));
}

/* ---------------------------------------------------------------------
 * system commands (SPEC-301 Behaviour 1 steps 7 and 8)
 * --------------------------------------------------------------------- */

void
Com_Error_f(void)
{
	Com_Error(ERR_FATAL, "%s", Cmd_Argv(1));
}

void
Com_Subliminal_f(void)
{
	/* text observed in the original (black box) */
	Com_Printf("WARNING: Subliminal messages enabled\n");
}

static void
Com_Minimise_f(void)
{
	cvar_t *noalttab = Cvar_Find("win_noalttab");

	if (noalttab && noalttab->value)
	{
		return;
	}

#ifdef _WIN32
	{
		HWND w = GetActiveWindow();

		if (w)
		{
			ShowWindow(w, SW_MINIMIZE);
		}
	}
#endif
}

static void
Com_Winstart_f(void)
{
	if (Cmd_Argc() < 2)
	{
		/* usage text observed in the original (black box) */
		Com_Printf("Usage: start <parameter>\n");
		return;
	}

	/* SoF runs "cmd.exe /c start <cmd>" after the program exits. A server
	   can send console commands, so OpenMerc does not run programs
	   (docs/DEVIATIONS.md). */
	Com_Printf("winstart is disabled in OpenMerc.\n");
}

static void
Com_Disconnect_f(void)
{
	SV_Shutdown("Server quit\n", false);
}

/* host name and local IPv4 addresses, as printed when networking starts */
void
SoF_PrintLocalAddresses(void)
{
	char host[256];
	struct addrinfo hints, *res, *a;
	int n = 0;

	if (gethostname(host, sizeof(host)))
	{
		return;
	}

	Com_Printf("Hostname: %s\n", host);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	if (getaddrinfo(host, NULL, &hints, &res))
	{
		return;
	}

	for (a = res; a; a = a->ai_next)
	{
		char ip[64];
		struct sockaddr_in *sin = (struct sockaddr_in *)a->ai_addr;

		inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
		Com_Printf("   LOCAL IP #%i: %s\n", ++n, ip);
	}

	freeaddrinfo(res);
}

static void
Com_IpList_f(void)
{
	SoF_PrintLocalAddresses();
}

/* ---------------------------------------------------------------------
 * OpenMerc test helper: om_delay <seconds> <command line> appends the
 * line to the command buffer after the delay (unlike "wait" it does not
 * hold back text the server stuffs in the meantime)
 * --------------------------------------------------------------------- */

#define OM_DELAY_MAX 32

static struct
{
	int due;
	char *text;
} om_delayed[OM_DELAY_MAX];

static void
Com_Delay_f(void)
{
	char text[MAX_STRING_CHARS];
	int i, j;

	if (Cmd_Argc() < 3)
	{
		Com_Printf("usage: om_delay <seconds> <command line>\n");
		return;
	}

	text[0] = 0;

	for (j = 2; j < Cmd_Argc(); j++)
	{
		if (j > 2)
		{
			Q_strlcat(text, " ", sizeof(text));
		}

		Q_strlcat(text, Cmd_Argv(j), sizeof(text));
	}

	for (i = 0; i < OM_DELAY_MAX; i++)
	{
		if (!om_delayed[i].text)
		{
			om_delayed[i].due = Sys_Milliseconds() + (int)(atof(Cmd_Argv(1)) * 1000);
			om_delayed[i].text = CopyString(text);
			return;
		}
	}

	Com_Printf("om_delay: queue full\n");
}

/* called once per main-loop pass */
void
SoF_RunDelayed(void)
{
	int i, now = Sys_Milliseconds();

	for (i = 0; i < OM_DELAY_MAX; i++)
	{
		if (om_delayed[i].text && (now - om_delayed[i].due >= 0))
		{
			Cbuf_AddText(om_delayed[i].text);
			Cbuf_AddText("\n");
			Z_Free(om_delayed[i].text);
			om_delayed[i].text = NULL;
		}
	}
}

#ifdef DEDICATED_ONLY
/* a dedicated server of SoF also has the key binding commands (its
   dedicated.cfg may bind keys); without a client they have no effect */
static void
Com_DedicatedKey_f(void)
{
}

void
SoF_InitDedicatedKeys(void)
{
	Cmd_AddCommand("bind", Com_DedicatedKey_f);
	Cmd_AddCommand("unbind", Com_DedicatedKey_f);
	Cmd_AddCommand("unbindall", Com_DedicatedKey_f);
	Cmd_AddCommand("bindlist", Com_DedicatedKey_f);
}
#endif

void
SoF_InitSystemCommands(void)
{
	Cmd_AddCommand("om_delay", Com_Delay_f);

	if (dedicated && dedicated->value)
	{
		Cmd_AddCommand("disconnect", Com_Disconnect_f);
	}

	Cmd_AddCommand("minimise", Com_Minimise_f);
	Cmd_AddCommand("winstart", Com_Winstart_f);
	Cmd_AddCommand("ip_list", Com_IpList_f);
}

/* ---------------------------------------------------------------------
 * autoexec.cfg rule (SPEC-301 Behaviour 4)
 * --------------------------------------------------------------------- */

void
Com_ExecAutoexec(void)
{
	char path[MAX_OSPATH];
	const char *gamedir = Cvar_VariableString("gamedir");

	Com_sprintf(path, sizeof(path), "%s/%s/autoexec.cfg",
			Cvar_VariableString("basedir"), gamedir[0] ? gamedir : BASEDIRNAME);

	if (FS_HostFileExists(path))
	{
		/* through the search path: a user copy wins */
		Cbuf_AddText("exec autoexec.cfg\n");
	}
}

/* ---------------------------------------------------------------------
 * config.cfg (SPEC-301 Behaviour 11)
 * --------------------------------------------------------------------- */

#ifndef DEDICATED_ONLY
void
Com_WriteConfig(qboolean keysonly)
{
	char path[MAX_OSPATH];
	FILE *f;
	cvar_t *var;
	int i;

	if (dedicated && dedicated->value)
	{
		return; /* a dedicated server never writes config.cfg */
	}

	if (keysonly)
	{
		Com_sprintf(path, sizeof(path), "%s/configs/CURRENT.cfg", FS_Userdir());
	}
	else
	{
		Com_sprintf(path, sizeof(path), "%s/config.cfg", FS_Userdir());
	}

	FS_CreatePath(path);

	/* text mode: CRLF line ends on Windows */
	f = Q_fopen(path, "w");

	if (!f)
	{
		Com_Printf("Could not write %s.\n", path);
		return;
	}

	fprintf(f, "// generated by Soldier of Fortune, do not modify\n");
	fprintf(f, "\n");
	fprintf(f, "unbindall\n");
	fprintf(f, "\n");

	for (i = 0; i < K_LAST; i++)
	{
		if (keybindings[i] && keybindings[i][0])
		{
			fprintf(f, "bind %s \"%s\"\n", Key_KeynumToString(i), keybindings[i]);
		}
	}

	if (!keysonly)
	{
		for (var = cvar_vars; var; var = var->next)
		{
			if (var->flags & CVAR_ARCHIVE)
			{
				fprintf(f, "set %s \"%s\"\n", var->name, var->string);
			}
		}
	}

	fclose(f);

	if (keysonly)
	{
		Com_Printf("Wrote key bindings to %s.\n", path);
	}
	else
	{
		Com_Printf("Wrote configuration to %s.\n", path);
	}
}

static void
Com_SaveConfig_f(void)
{
	Com_WriteConfig(false);
}

static void
Com_SaveKeys_f(void)
{
	Com_WriteConfig(true);
}

/* vid_modes (SPEC-313 §12) */
static void
Com_VidModes_f(void)
{
	static const char *modes[10] = {
		"Unsupported", "Unsupported", "Unsupported", "640x480", "800x600",
		"960x720", "1024x768", "1152x864", "1280x960", "1600x1200"
	};
	int i;

	for (i = 0; i < 10; i++)
	{
		Com_Printf("Mode %i: %s\n", i, modes[i]);
	}
}

static void
Com_MaxFpsFromRate_f(void)
{
	int rate = (int)Cvar_VariableValue("rate");

	if (rate < 5000)
	{
		Cvar_Set("cl_maxfps", "30");
	}
	else if (rate < 10000)
	{
		Cvar_Set("cl_maxfps", "60");
	}
	else
	{
		Cvar_Set("cl_maxfps", "120");
	}
}

static void
Com_Exit_f(void)
{
	Cbuf_AddText("disconnect\nquit\n");
}

/* userweapons / useritems / userammo / usermisc: the inventory cvars of
   one flag, newest first, "%-20s%s" (format observed in the original) */
static void
Com_ListFlag(const char *title, int flag)
{
	cvar_t *var;

	Com_Printf("%s\n", title);

	for (var = cvar_vars; var; var = var->next)
	{
		if ((var->flags & flag) && !(var->flags & CVAR_HIDDEN))
		{
			Com_Printf("%-20s%s\n", var->name, var->string);
		}
	}
}

static void
Com_UserWeapons_f(void)
{
	Com_ListFlag("Weapons:", CVAR_WEAPON);
}

static void
Com_UserItems_f(void)
{
	Com_ListFlag("Items:", CVAR_ITEM);
}

static void
Com_UserAmmo_f(void)
{
	Com_ListFlag("Ammo:", CVAR_AMMO);
}

static void
Com_UserMisc_f(void)
{
	Com_ListFlag("Misc:", CVAR_MISC);
}

/* conchars (SPEC-313 §11): character and colour samples */
static void
Com_Conchars_f(void)
{
	char line[64];
	int row, i;

	Com_Printf("Displayable characters :\n");
	Com_Printf("\n");

	for (row = 0; row < 7; row++)
	{
		line[0] = 0x07; /* cyan */
		line[1] = ':';

		for (i = 0; i < 32; i++)
		{
			line[2 + i] = (char)(32 + row * 32 + i);
		}

		line[34] = '\n';
		line[35] = 0;
		Com_Printf("%s", line);
	}

	Com_Printf("Displayable colors :\n");
	Com_Printf("\n");

	for (i = 1; i < 32; i++)
	{
		line[0] = ((i == 10) || (i == 13)) ? ' ' : (char)i;
		line[1] = '@';
		line[2] = 0;
		Com_Printf("%s", line);
	}

	Com_Printf("\n");
}

void
SoF_InitClientCommands(void)
{
	if (dedicated && dedicated->value)
	{
		return;
	}

	Cmd_AddCommand("saveconfig", Com_SaveConfig_f);
	Cmd_AddCommand("savekeys", Com_SaveKeys_f);
	Cmd_AddCommand("maxfpsfromrate", Com_MaxFpsFromRate_f);
	Cmd_AddCommand("exit", Com_Exit_f);
	Cmd_AddCommand("conchars", Com_Conchars_f);

	/* SPEC-302 lists these under "engine (menus)"; they only print the
	   inventory cvars, so the engine core provides them */
	if (!Cmd_Exists("userweapons"))
	{
		Cmd_AddCommand("userweapons", Com_UserWeapons_f);
		Cmd_AddCommand("useritems", Com_UserItems_f);
		Cmd_AddCommand("userammo", Com_UserAmmo_f);
		Cmd_AddCommand("usermisc", Com_UserMisc_f);
	}

	if (!Cmd_Exists("vid_modes"))
	{
		Cmd_AddCommand("vid_modes", Com_VidModes_f);
	}
}
#else
void
Com_WriteConfig(qboolean keysonly)
{
	(void)keysonly;
}

void
SoF_InitClientCommands(void)
{
}
#endif
