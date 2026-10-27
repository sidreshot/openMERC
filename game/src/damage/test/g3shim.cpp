// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc G3 - game-module shim for black-box tests of the damage package (test tool, not shipped).
//
// Installed as base/gamex86.dll of the G3 test copy. It loads the module under test (base/gamex86_real.dll: the
// original module or OpenMerc's) and hands it a copy of the engine's import table (SPEC-001, 99 entries) in which only
// the lookups of the parental-lock cvars are answered by the shim, so gore can be compared with the locks off
// without touching the engine's lock store (the registry):
//   +set g3_violence <n>   before the map loads: the `lock_*` cvars the module reads (imports #87 and #92) take the
//                          bits of n (SPEC-313 2: 1 lock_deaths, 2 lock_blood, 4 lock_gorezones, 8 lock_sever,
//                          16 lock_textures, 32 lock_pure); absent or "" = no override (the engine's values).
//   +set g3_sp 1           deathmatch is force-set to 0 first (single player on a dedicated server, as G2's spshim).
//   +set g3_heapcheck 1    every process heap is validated (HeapValidate) after Init, SpawnEntities, every
//                          RunFrame, ClientDisconnect and before/after Shutdown; failures and a summary go to
//                          `g3heap.log` in the working directory (lifecycle tests of the module under test).
// The engine's own lock cvars, sv_violence and the registry are untouched; nothing else is changed or traced.
//
// Implements: SPEC-001 (GetGameAPI, imports #87, #90, #92), SPEC-313 2 (lock cvar names and bits)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *(*getgameapi_t)(void *import);
typedef void (*cvarcb_t)(void *cvar);
typedef void *(*cvar_get_t)(const char *name, const char *value, int flags, cvarcb_t cb);
typedef void *(*cvar_forceset_t)(const char *name, const char *value);
typedef float (*cvar_value_t)(const char *name);

enum { NUM_IMPORTS = 99, IMP_CVAR = 87, IMP_FORCESET = 90, IMP_CVAR_VALUE = 92 };

// cvar record as the game reads it (docs/impl/G18.md "Cvar record": name 0, string 4, latched 8, flags 0x0C,
// value float 0x18); the rest stays zero
struct FakeCvar {
	const char *name;    // 0x00
	const char *string;  // 0x04
	const char *latched; // 0x08
	int flags;           // 0x0C
	int unknown10;       // 0x10
	int unknown14;       // 0x14
	float value;         // 0x18
	void *next;          // 0x1C
	int pad[8];
};

static const char *const lockNames[6] = {"lock_deaths", "lock_blood", "lock_gorezones", "lock_sever", "lock_textures",
                                         "lock_pure"};
static FakeCvar fakes[6];
static void *table[NUM_IMPORTS];
static cvar_get_t realCvar;
static cvar_value_t realCvarValue;
static int violence = -1; // -1 = no override

static FakeCvar *Fake(const char *name)
{
	if (violence < 0 || !name)
		return NULL;
	for (int i = 0; i < 6; i++)
		if (!strcmp(name, lockNames[i]))
			return &fakes[i];
	return NULL;
}

static void *ShimCvar(const char *name, const char *value, int flags, cvarcb_t cb)
{
	FakeCvar *f = Fake(name);
	if (!f)
		return realCvar(name, value, flags, cb);
	if (cb)
		cb(f);
	return f;
}

static float ShimCvarValue(const char *name)
{
	FakeCvar *f = Fake(name);
	return f ? f->value : realCvarValue(name);
}

// ---- heap checks (export table entries of SPEC-001: Init 0x04, Shutdown 0x08, SpawnEntities 0x0C,
// ClientDisconnect 0x30, RunFrame 0x5C) ----
typedef void (*fn_void)(void);
typedef void (*fn_spawn)(const char *, const char *, const char *);
typedef void (*fn_ent)(void *);
typedef float (*fn_frame)(int);
static fn_void realInit, realShutdown;
static fn_spawn realSpawn;
static fn_ent realDisconnect;
static fn_frame realRunFrame;
static int heapChecks, heapFailures, lastFrame;

static void HeapLog(const char *fmt, const char *where, int frame, void *heap)
{
	FILE *f = fopen("g3heap.log", "a");
	if (!f)
		return;
	fprintf(f, fmt, where, frame, heap);
	fclose(f);
}

static void CheckHeaps(const char *where)
{
	HANDLE heaps[256];
	DWORD n = GetProcessHeaps(256, heaps);
	heapChecks++;
	for (DWORD i = 0; i < n && i < 256; i++)
		if (!HeapValidate(heaps[i], 0, NULL)) {
			heapFailures++;
			if (heapFailures <= 20)
				HeapLog("INVALID heap after %s (frame %d): %p\n", where, lastFrame, heaps[i]);
		}
}

static void HInit(void) { realInit(); CheckHeaps("Init"); }
static void HShutdown(void)
{
	CheckHeaps("before Shutdown");
	realShutdown();
	CheckHeaps("Shutdown");
	char line[128];
	_snprintf(line, sizeof line, "summary: %d checks, %d failures", heapChecks, heapFailures);
	HeapLog("%s (last frame %d) %p\n", line, lastFrame, NULL);
}
static void HSpawn(const char *m, const char *e, const char *sp)
{
	realSpawn(m, e, sp);
	CheckHeaps("SpawnEntities");
	FILE *f = fopen("g3heap.log", "a");
	if (f) {
		fprintf(f, "SpawnEntities %s: %d checks, %d failures so far\n", m ? m : "?", heapChecks, heapFailures);
		fclose(f);
	}
}
static void HDisconnect(void *ent) { realDisconnect(ent); CheckHeaps("ClientDisconnect"); }
static float HRunFrame(int frame)
{
	lastFrame = frame;
	float r = realRunFrame(frame);
	CheckHeaps("RunFrame");
	return r;
}

static void InstallHeapChecks(void *exports)
{
	void **ex = (void **)exports;
	realInit = (fn_void)ex[0x04 / 4];
	realShutdown = (fn_void)ex[0x08 / 4];
	realSpawn = (fn_spawn)ex[0x0C / 4];
	realDisconnect = (fn_ent)ex[0x30 / 4];
	realRunFrame = (fn_frame)ex[0x5C / 4];
	ex[0x04 / 4] = (void *)HInit;
	ex[0x08 / 4] = (void *)HShutdown;
	ex[0x0C / 4] = (void *)HSpawn;
	ex[0x30 / 4] = (void *)HDisconnect;
	ex[0x5C / 4] = (void *)HRunFrame;
}

extern "C" __declspec(dllexport) void *GetGameAPI(void *import)
{
	void **imp = (void **)import;
	memcpy(table, imp, sizeof table);
	realCvar = (cvar_get_t)imp[IMP_CVAR];
	realCvarValue = (cvar_value_t)imp[IMP_CVAR_VALUE];

	// options (cvars set on the command line before the map loads)
	const char **sp = (const char **)realCvar("g3_sp", "0", 0, NULL);
	if (sp && sp[1] && atoi(sp[1]) != 0)
		((cvar_forceset_t)imp[IMP_FORCESET])("deathmatch", "0");
	const char **v = (const char **)realCvar("g3_violence", "", 0, NULL);
	if (v && v[1] && v[1][0]) {
		violence = atoi(v[1]);
		for (int i = 0; i < 6; i++) {
			int on = (violence >> i) & 1;
			fakes[i].name = lockNames[i];
			fakes[i].string = on ? "1" : "0";
			fakes[i].flags = 0x1080; // hidden, protected (SPEC-302)
			fakes[i].value = (float)on;
		}
		table[IMP_CVAR] = (void *)ShimCvar;
		table[IMP_CVAR_VALUE] = (void *)ShimCvarValue;
	}

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
	void *exports = api ? api(table) : NULL;
	const char **hc = (const char **)realCvar("g3_heapcheck", "0", 0, NULL);
	if (exports && hc && hc[1] && atoi(hc[1]) != 0)
		InstallHeapChecks(exports);
	return exports;
}
