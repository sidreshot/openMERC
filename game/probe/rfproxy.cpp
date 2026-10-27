// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc G18 - RunFrame logging proxy (test tool, not shipped).
//
// Loaded by the engine as base\gamex86.dll, it loads the module under test (env G18_RF_TARGET, a file name in
// base\, default gamex86.dll.orig), forwards every export unchanged and logs (env G18_RF_LOG) each RunFrame
// argument with the time the module returns, and the SpawnEntities / ClientBegin calls, so the game-time rule
// of export 0x5C can be compared between the original module and OpenMerc's. Only the interface is exercised.
//
// Implements: SPEC-001 (export table: RunFrame 0x5C)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/sof_game.h"

typedef game_export_t *(*GetGameAPI_t)(game_import_t *);

static game_export_t *g_real;
static game_export_t g_saved;
static FILE *g_log;
static int g_calls;

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

static void proxy_SpawnEntities(const char *map, const char *ents, const char *spawnpoint)
{
	plog("spawn %s (after %d RunFrame calls)\n", map, g_calls);
	g_saved.SpawnEntities(map, ents, spawnpoint);
	plog("spawn done\n");
}

static float proxy_RunFrame(int framenum)
{
	float t = g_saved.RunFrame(framenum);
	g_calls++;
	if (g_calls <= 40 || g_calls % 50 == 0)
		plog("call %d RunFrame(%d) -> %.4f\n", g_calls, framenum, t);
	return t;
}

static void proxy_ClientBegin(edict_t *ent)
{
	plog("ClientBegin after %d RunFrame calls\n", g_calls);
	g_saved.ClientBegin(ent);
}

extern "C" __declspec(dllexport) game_export_t *GetGameAPI(game_import_t *import)
{
	char base[MAX_PATH], path[MAX_PATH];
	GetModuleFileNameA(GetModuleHandleA("gamex86.dll"), base, sizeof(base));
	char *slash = strrchr(base, '\\');
	if (slash)
		slash[1] = 0;
	const char *target = getenv("G18_RF_TARGET");
	_snprintf(path, sizeof(path), "%s%s", base, target && *target ? target : "gamex86.dll.orig");
	const char *logname = getenv("G18_RF_LOG");
	if (logname && *logname && !g_log)
		g_log = fopen(logname, "w");
	plog("rfproxy loading %s\n", path);
	HMODULE h = LoadLibraryA(path);
	if (!h)
		return NULL;
	GetGameAPI_t real = (GetGameAPI_t)GetProcAddress(h, "GetGameAPI");
	if (!real)
		return NULL;
	g_real = real(import);
	if (!g_real)
		return NULL;
	g_saved = *g_real;
	g_real->SpawnEntities = proxy_SpawnEntities;
	g_real->RunFrame = proxy_RunFrame;
	g_real->ClientBegin = proxy_ClientBegin;
	return g_real;
}
