// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc G2 - single-player shim for black-box tests (test tool, not shipped).
//
// A dedicated server of the original engine always initialises the game in deathmatch. To observe single-player
// behaviour (characters, scripts) through a dedicated server, this tiny module is installed as base/gamex86.dll in
// a test copy of the game: it sets the `deathmatch` cvar back to 0 (SPEC-001 import #90, cvar_forceset) when the
// engine asks for the game interface, then loads the game module under test (base/gamex86_real.dll, the original
// or OpenMerc's) and returns its interface. With the environment variable G2_SHIM_MAXCLIENTS1=1 it also wraps the
// module's Init (export 0x04) to force `maxclients` 1 before it runs (needed for `save`; it shifts every entity
// number by 7, so captures with and without it are not comparable). Only the SPEC-001 interface is used; nothing
// is traced.
//
// Implements: SPEC-001 (GetGameAPI, import #90, export Init)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <string.h>

typedef void *(*getgameapi_t)(void *import);
typedef void *(*cvar_forceset_t)(const char *name, const char *value);
typedef void (*init_t)(void);

static cvar_forceset_t g_forceset;
static init_t g_real_init;

static void ShimInit(void)
{
	g_forceset("deathmatch", "0");
	g_forceset("maxclients", "1");
	if (g_real_init)
		g_real_init();
}

extern "C" __declspec(dllexport) void *GetGameAPI(void *import)
{
	// import table entry 90 (SPEC-001): cvar_forceset(name, value)
	g_forceset = ((cvar_forceset_t *)import)[90];
	g_forceset("deathmatch", "0");

	char path[MAX_PATH];
	HMODULE self = NULL;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   (LPCSTR)&GetGameAPI, &self);
	GetModuleFileNameA(self, path, sizeof(path));
	char *slash = strrchr(path, '\\');
	if (!slash)
		return NULL;
	strcpy(slash + 1, "gamex86_real.dll");
	HMODULE real = LoadLibraryA(path);
	if (!real)
		return NULL;
	getgameapi_t api = (getgameapi_t)GetProcAddress(real, "GetGameAPI");
	void *ge = api ? api(import) : NULL;
	const char *mc1 = getenv("G2_SHIM_MAXCLIENTS1");
	if (ge && mc1 && atoi(mc1)) {
		// export table (SPEC-001): 0x00 apiversion, 0x04 Init
		init_t *slot = (init_t *)((char *)ge + 4);
		g_real_init = *slot;
		*slot = ShimInit;
	}
	return ge;
}
