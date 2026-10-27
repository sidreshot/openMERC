// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc G10 - test proxy game module (test tool, never shipped).
//
// Installed as base\gamex86.dll of the test copy, it loads the module under test (env G10_PROXY_TARGET, a file name
// in base\: the original gamex86.dll.orig or OpenMerc's gamex86.om.dll), forwards every export and import, and:
// - G10_PROXY_PURE=1: after each game frame sets bit 0x20 ("pure server") in cvar sv_violence through the engine's
//   own cvar import (#90). The engine sets that bit only for the shipped module's file checksum (SPEC-AMEND-005
//   GS.15); the proxy is the file the engine loaded, so without this a client whose parental locks are all on
//   (cl_violence 63, the state of this PC's lock store) is refused (`rejected_violence`). Both modules get the same
//   treatment, and the lock bits 0x01-0x10 stay the server's own.
// - G10_PROXY_SP=1: force-sets deathmatch 0 and maxclients 1 before the module's Init (as G6b's spproxy).
// - G10_PROXY_LOG=<file>: logs the savegame exports (0x10-0x1C, 0x40, 0x44) and every chunk the module writes or
//   reads through imports #85/#86 (id, length) between them.
// Only the interface tables are used; no engine or game code is read.
//
// Implements: SPEC-001 (export/import tables, imports #85/#86/#90/#92), SPEC-AMEND-005 GS.15 (pure bit)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../sof_game.h"

typedef game_export_t *(*GetGameAPI_t)(game_import_t *);

static game_import_t g_imp;   // the engine's table (copied)
static game_import_t g_mine;  // what the module under test gets
static game_export_t *g_real;
static game_export_t g_saved;
static FILE *g_log;
static int g_pure, g_sp, g_inside;
static int g_chunks_w, g_chunks_r;
static HMODULE g_module;

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

static void idtext(int id, char *o)
{
	o[0] = (char)(id >> 24);
	o[1] = (char)(id >> 16);
	o[2] = (char)(id >> 8);
	o[3] = (char)id;
	o[4] = 0;
}

static int px_WriteChunk(int id, const void *data, int length)
{
	char t[5];
	idtext(id, t);
	g_chunks_w++;
	if (g_log && g_inside)
		plog("  w %s %d\n", t, length);
	return g_imp.WriteChunk(id, data, length);
}

static int px_ReadChunk(int id, void *buffer, int length, void **allocated)
{
	char t[5];
	idtext(id, t);
	int n = g_imp.ReadChunk(id, buffer, length, allocated);
	g_chunks_r++;
	if (g_log && g_inside)
		plog("  r %s %d -> %d\n", t, length, n);
	return n;
}

static void proxy_Init(void)
{
	if (g_sp) {
		g_imp.cvar_forceset("deathmatch", "0");
		g_imp.cvar_forceset("maxclients", "1");
	}
	plog("init deathmatch %g maxclients %g\n", g_imp.cvar_value("deathmatch"), g_imp.cvar_value("maxclients"));
	g_saved.Init();
}

static void pure(void)
{
	if (!g_pure)
		return;
	int v = (int)g_imp.cvar_value("sv_violence");
	if (!(v & 0x20)) {
		char buf[16];
		sprintf(buf, "%d", v | 0x20);
		g_imp.cvar_forceset("sv_violence", buf);
		plog("sv_violence %d -> %s\n", v, buf);
	}
}

// The engine unloads the game module file it loaded (this proxy) when the game shuts down (every map/load restart);
// the module under test must go with it, so that its next Init starts from a fresh image as without a proxy.
static void proxy_Shutdown(void)
{
	plog("shutdown\n");
	g_saved.Shutdown();
	if (g_module) {
		FreeLibrary(g_module);
		g_module = NULL;
	}
}

static void proxy_SpawnEntities(const char *map, const char *ents, const char *spawnpoint)
{
	plog("spawn %s\n", map);
	g_saved.SpawnEntities(map, ents, spawnpoint);
}

static float proxy_RunFrame(int framenum)
{
	float t = g_saved.RunFrame(framenum);
	if (framenum % 50 == 0)
		plog("frame %d time %.1f\n", framenum, t);
	pure();
	return t;
}

static qboolean proxy_ClientPreConnect(edict_t *ent, char *userinfo)
{
	pure();
	return g_saved.ClientPreConnect(ent, userinfo);
}

static qboolean proxy_ClientConnect(edict_t *ent, char *userinfo)
{
	pure();
	return g_saved.ClientConnect(ent, userinfo);
}

static void proxy_WriteGame(qboolean autosave)
{
	// the automatic save of a map start comes after the engine's end-of-spawn sv_violence update and before the
	// local client connects
	pure();
	plog("WriteGame(%d)\n", (int)autosave);
	g_inside = 1;
	g_saved.WriteGame(autosave);
	g_inside = 0;
}

static qboolean proxy_ReadGame(int unused)
{
	plog("ReadGame(%d)\n", unused);
	g_inside = 1;
	qboolean r = g_saved.ReadGame(unused);
	g_inside = 0;
	plog("ReadGame -> %d\n", (int)r);
	return r;
}

static void proxy_WriteLevel(void)
{
	plog("WriteLevel\n");
	g_inside = 1;
	g_chunks_w = 0;
	g_saved.WriteLevel();
	g_inside = 0;
	plog("WriteLevel: %d chunks\n", g_chunks_w);
}

static void proxy_ReadLevel(void)
{
	plog("ReadLevel\n");
	g_inside = 1;
	g_chunks_r = 0;
	g_saved.ReadLevel();
	g_inside = 0;
	plog("ReadLevel: %d chunks\n", g_chunks_r);
	pure();
}

static qboolean proxy_SaveAllowed(void)
{
	qboolean r = g_saved.SaveAllowed();
	plog("SaveAllowed -> %d\n", (int)r);
	return r;
}

extern "C" __declspec(dllexport) game_export_t *GetGameAPI(game_import_t *import)
{
	char base[MAX_PATH], path[MAX_PATH];
	GetModuleFileNameA(GetModuleHandleA("gamex86.dll"), base, sizeof(base));
	char *slash = strrchr(base, '\\');
	if (slash)
		slash[1] = 0;
	const char *target = getenv("G10_PROXY_TARGET");
	_snprintf(path, sizeof(path), "%s%s", base, target && *target ? target : "gamex86.dll.orig");
	const char *logname = getenv("G10_PROXY_LOG");
	if (logname && *logname && !g_log)
		g_log = fopen(logname, "a");
	g_pure = getenv("G10_PROXY_PURE") && atoi(getenv("G10_PROXY_PURE"));
	g_sp = getenv("G10_PROXY_SP") && atoi(getenv("G10_PROXY_SP"));
	plog("proxy loading %s (pure %d, sp %d)\n", path, g_pure, g_sp);
	HMODULE h = LoadLibraryA(path);
	g_module = h;
	if (!h) {
		plog("LoadLibrary failed %lu\n", GetLastError());
		return NULL;
	}
	GetGameAPI_t real = (GetGameAPI_t)GetProcAddress(h, "GetGameAPI");
	if (!real)
		return NULL;
	g_imp = *import;
	g_mine = *import;
	g_mine.WriteChunk = px_WriteChunk;
	g_mine.ReadChunk = px_ReadChunk;
	g_real = real(&g_mine);
	if (!g_real)
		return NULL;
	g_saved = *g_real;
	g_real->Init = proxy_Init;
	g_real->Shutdown = proxy_Shutdown;
	g_real->SpawnEntities = proxy_SpawnEntities;
	g_real->RunFrame = proxy_RunFrame;
	g_real->WriteGame = proxy_WriteGame;
	g_real->ReadGame = proxy_ReadGame;
	g_real->WriteLevel = proxy_WriteLevel;
	g_real->ReadLevel = proxy_ReadLevel;
	g_real->SaveAllowed = proxy_SaveAllowed;
	g_real->ClientPreConnect = proxy_ClientPreConnect;
	g_real->ClientConnect = proxy_ClientConnect;
	return g_real;
}
