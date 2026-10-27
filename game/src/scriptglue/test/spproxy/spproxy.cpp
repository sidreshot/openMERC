// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc G6b - single-player proxy game module (test tool, not shipped).
//
// The original SoF.exe forces deathmatch on a dedicated server, so single-player maps (tsr1, arm1, trn1) abort
// there ("Couldn't find deathmatch spawn point", sound overflow). This proxy is loaded by the engine as
// base\gamex86.dll, loads the game module under test (env G6B_PROXY_TARGET, a file name in base\: the original
// gamex86.dll.orig or OpenMerc's gamex86.om.dll) and forwards every export unchanged, except that before the
// module's Init it force-sets `deathmatch` 0 and `maxclients` 1 through the engine's own cvar import (SPEC-001
// #90), so the module under test runs its single-player rules. Both modules get exactly the same treatment.
// Optionally (env G6B_PROXY_LOG) it logs, per game frame, the cinematic state the module reports (export 0x54)
// and every SetCinematicState call of the engine (export 0x58).
// Only the interface is exercised; no engine or game code is read.
//
// Implements: SPEC-001 (export/import tables, cinematic exports 0x54/0x58)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../sof_game.h"

typedef game_export_t *(*GetGameAPI_t)(game_import_t *);

static game_import_t g_impcopy; // the engine passes a temporary table (copied, as every game module does)
static game_import_t *g_imp = &g_impcopy;
static game_export_t *g_real;
static game_export_t g_saved; // the module's own entries
static FILE *g_log;
static int g_lastcin = -1;
static int g_frame;

static void plog(const char *fmt, ...)
{
	if (!g_log)
		return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(g_log, fmt, ap);
	va_end(ap);
	fflush(g_log);
}

static void proxy_Init(void)
{
	plog("init: forcing single player\n");
	g_imp->cvar_forceset("deathmatch", "0");
	plog("deathmatch set\n");
	g_imp->cvar_forceset("maxclients", "1");
	plog("init deathmatch %g maxclients %g\n", g_imp->cvar_value("deathmatch"), g_imp->cvar_value("maxclients"));
	g_saved.Init();
}

static void proxy_SpawnEntities(const char *map, const char *ents, const char *spawnpoint)
{
	plog("spawn %s\n", map);
	g_lastcin = -1;
	g_saved.SpawnEntities(map, ents, spawnpoint);
}

static float proxy_RunFrame(int framenum)
{
	g_frame = framenum;
	float t = g_saved.RunFrame(framenum);
	int c = g_saved.GetCinematicState();
	if (c != g_lastcin) {
		plog("frame %d time %.1f cinematic %d\n", framenum, t, c);
		g_lastcin = c;
	}
	return t;
}

static void proxy_SetCinematicState(int s)
{
	plog("frame %d engine sets cinematic %d\n", g_frame, s);
	g_saved.SetCinematicState(s);
}

extern "C" __declspec(dllexport) game_export_t *GetGameAPI(game_import_t *import)
{
	char base[MAX_PATH], path[MAX_PATH];
	GetModuleFileNameA(GetModuleHandleA("gamex86.dll"), base, sizeof(base));
	char *slash = strrchr(base, '\\');
	if (slash)
		slash[1] = 0;
	const char *target = getenv("G6B_PROXY_TARGET");
	_snprintf(path, sizeof(path), "%s%s", base, target && *target ? target : "gamex86.dll.orig");
	const char *logname = getenv("G6B_PROXY_LOG");
	if (logname && *logname && !g_log)
		g_log = fopen(logname, "w");
	plog("proxy loading %s\n", path);
	HMODULE h = LoadLibraryA(path);
	if (!h) {
		plog("LoadLibrary failed %lu\n", GetLastError());
		return NULL;
	}
	GetGameAPI_t real = (GetGameAPI_t)GetProcAddress(h, "GetGameAPI");
	if (!real)
		return NULL;
	g_impcopy = *import;
	plog("calling GetGameAPI\n");
	g_real = real(import);
	plog("GetGameAPI returned %p\n", (void *)g_real);
	if (!g_real)
		return NULL;
	g_saved = *g_real;
	g_real->Init = proxy_Init;
	g_real->SpawnEntities = proxy_SpawnEntities;
	g_real->RunFrame = proxy_RunFrame;
	g_real->SetCinematicState = proxy_SetCinematicState;
	return g_real;
}
