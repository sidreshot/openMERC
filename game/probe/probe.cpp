// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc G18 - interface probe game module (test tool, not shipped).
//
// A minimal SPEC-001 game module that the original engine loads like gamex86.dll. It fills the
// unspecified engine-visible structures (entity state, player state) with known patterns, one 32-bit
// word per server frame, and encodes which word it set in the game time it returns, so that a headless
// client can read back how the engine puts each memory word on the wire (SPEC-202). It also dumps the
// user commands the engine hands to ClientThink, the cvar record returned by the cvar import and the
// surface record of a trace. Black-box: only the interface is exercised, no engine code is read.
//
// Implements: SPEC-001 (exports/imports), SPEC-760 1 (entity prefix)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <utility>

#include "../src/sof_game.h"

static game_import_t gi;
static game_export_t ge;

#define EDICT_SIZE 1124
#define CLIENT_SIZE 1536
static uint8_t g_edicts[MAX_EDICTS * EDICT_SIZE];
static uint8_t g_clients[MAX_CLIENTS * CLIENT_SIZE];
static int g_maxclients = 1;
static int g_mode = 0;
static FILE *g_log;
static int g_word = -1, g_sub = 0, g_target = 0;
static int g_started = -1;
static float g_spawn[3];

static uint8_t *EDICT(int n) { return g_edicts + n * EDICT_SIZE; }
static int &I(uint8_t *e, int ofs) { return *(int *)(e + ofs); }
static float &F(uint8_t *e, int ofs) { return *(float *)(e + ofs); }

static void plog(const char *fmt, ...)
{
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	va_end(ap);
	buf[sizeof(buf) - 1] = 0;
	if (g_log) {
		fputs(buf, g_log);
		fflush(g_log);
	}
}

static void hexdump(const char *tag, const void *p, int n)
{
	const uint8_t *b = (const uint8_t *)p;
	plog("%s:", tag);
	for (int i = 0; i < n; i++)
		plog("%s%02x", (i % 4) ? "" : " ", b[i]);
	plog("\n");
}

static LONG CALLBACK veh(PEXCEPTION_POINTERS ep)
{
	if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
		plog("FAULT access violation: %s address %08x, probe target %d word %d sub %d\n",
		     ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
		     (unsigned)ep->ExceptionRecord->ExceptionInformation[1], g_target, g_word, g_sub);
		TerminateProcess(GetCurrentProcess(), 3);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

static void link_world()
{
	uint8_t *w = EDICT(0);
	memset(w, 0, EDICT_SIZE);
	I(w, 0x78) = 1;     // inuse
	I(w, 0x20) = 1;     // s.modelindex (SPEC-760 1)
	I(w, 0x158) = 3;    // SOLID_BSP
}

static void Init(void)
{
	char path[512];
	_snprintf(path, sizeof(path), "%s/g18probe.log", gi.FS_Gamedir());
	g_log = fopen(path, "w");
	g_maxclients = (int)gi.cvar_value("maxclients");
	if (g_maxclients < 1)
		g_maxclients = 1;
	g_mode = (int)gi.cvar_value("probe_mode");
	plog("probe Init maxclients %d mode %d gamedir %s\n", g_maxclients, g_mode, gi.FS_Gamedir());
	// cvar record layout
	cvar_t *cv = gi.cvar("probe_cv", "12.5", 0, NULL);
	plog("cvar probe_cv at %p\n", (void *)cv);
	hexdump("cvar bytes", cv, 48);
	for (int o = 0; o < 48; o += 4) {
		uint32_t v = *(uint32_t *)((uint8_t *)cv + o);
		if (v > 0x10000 && !IsBadReadPtr((void *)v, 8))
			plog("  +%02x -> ptr \"%.20s\"\n", o, (const char *)v);
		plog("  +%02x int %d float %g\n", o, (int)v, *(float *)((uint8_t *)cv + o));
	}
	gi.cvar_set("probe_cv", "7.25");
	hexdump("cvar bytes after set 7.25", cv, 48);
	ge.num_edicts = g_maxclients + 1;
	AddVectoredExceptionHandler(1, veh);
}

static void Shutdown(void)
{
	plog("probe Shutdown\n");
	if (g_log)
		fclose(g_log);
	g_log = NULL;
}

static void SpawnEntities(const char *mapname, const char *ents, const char *spawn)
{
	plog("SpawnEntities %s (%d chars) spawn '%s'\n", mapname, (int)strlen(ents), spawn);
	memset(g_edicts, 0, sizeof(g_edicts));
	for (int i = 0; i < MAX_EDICTS; i++)
		I(EDICT(i), 0) = i;
	link_world();
	for (int i = 0; i < g_maxclients; i++)
		*(uint8_t **)(EDICT(i + 1) + 0x74) = g_clients + i * CLIENT_SIZE;
	gi.configstring(CS_MAXCLIENTS, "8");
	gi.linkentity((edict_t *)EDICT(0));
	ge.num_edicts = g_maxclients + 1;

	// surface record probe: trace straight down and straight up from a point inside the map
	trace_t tr;
	float start[3] = {0, 0, 0}, end[3] = {0, 0, -8192}, end2[3] = {0, 0, 8192};
	const char *s = strstr(ents, "info_player_deathmatch");
	if (!s) s = strstr(ents, "info_player_start");
	if (s) {
		const char *o = strstr(s, "\"origin\"");
		if (!o) {
			// origin may come before classname: search backwards for the block start
			const char *b = s;
			while (b > ents && *b != '{') b--;
			o = strstr(b, "\"origin\"");
		}
		if (o)
			sscanf(o + 8, " \"%f %f %f\"", &start[0], &start[1], &start[2]);
	}
	memcpy(g_spawn, start, sizeof(g_spawn));
	end[0] = end2[0] = start[0];
	end[1] = end2[1] = start[1];
	end2[2] = start[2] + 8192;
	end[2] = start[2] - 8192;
	for (int k = 0; k < 2; k++) {
		memset(&tr, 0xEE, sizeof(tr));
		int r = gi.trace(start, NULL, NULL, k ? end2 : end, NULL, 1, &tr);
		plog("trace %s from %g %g %g: ret %d frac %g end %g %g %g normal %g %g %g contents %x ent %p leaf %d surf %p\n",
		     k ? "up" : "down", start[0], start[1], start[2], r, tr.fraction, tr.endpos[0], tr.endpos[1],
		     tr.endpos[2], tr.plane.normal[0], tr.plane.normal[1], tr.plane.normal[2], tr.contents,
		     (void *)tr.ent, tr.leafnum, (void *)tr.surface);
		hexdump("trace bytes", &tr, 56);
		MEMORY_BASIC_INFORMATION mbi;
		if (tr.surface && VirtualQuery(tr.surface, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT)
			hexdump("surface bytes", tr.surface, 128);
	}
}

static qboolean ClientPreConnect(edict_t *, char *) { return 1; }

static qboolean ClientConnect(edict_t *ent, char *userinfo)
{
	uint8_t *e = (uint8_t *)ent;
	int n = (int)((e - g_edicts) / EDICT_SIZE);
	plog("ClientConnect ent %d userinfo %s\n", n, userinfo);
	*(uint8_t **)(e + 0x74) = g_clients + (n - 1) * CLIENT_SIZE;
	return 1;
}

static void ClientBegin(edict_t *ent)
{
	uint8_t *e = (uint8_t *)ent;
	int n = (int)((e - g_edicts) / EDICT_SIZE);
	plog("ClientBegin ent %d\n", n);
	I(e, 0x78) = 1;
	g_started = -1;
}

static void ClientUserinfoChanged(edict_t *ent, char *ui, qboolean ingame)
{
	plog("ClientUserinfoChanged ingame %d: %s\n", ingame, ui);
}

static void ClientDisconnect(edict_t *ent)
{
	I((uint8_t *)ent, 0x78) = 0;
	plog("ClientDisconnect\n");
}

static void ClientCommand(edict_t *ent)
{
	plog("ClientCommand argc %d: %s\n", gi.argc(), gi.args());
}

// ---- player-movement block probe (probe_mode 4/8) ------------------------------------------------
// Phase 1 (mode 4): every word of the block after the state (28 bytes) and the command (24 bytes)
// points to a distinct stub; the stubs log their index (= block offset / 4) and their arguments.
// A stub called like Quake II's trace (hidden result pointer on the stack + 4 vector pointers)
// returns "nothing hit"; anything else returns 0.
#define PM_BLOCK 2048
static uint8_t g_pm[PM_BLOCK];
static int g_pmcalls;
static edict_t *g_pmself;

static bool readable(uint32_t p, int n)
{
	MEMORY_BASIC_INFORMATION mbi;
	if (p < 0x10000 || !VirtualQuery((void *)p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
		return false;
	if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
		return false;
	return (uint32_t)mbi.BaseAddress + mbi.RegionSize >= p + n;
}

static uint32_t stub_common(int n, const uint32_t *a)
{
	uint32_t here = (uint32_t)(uintptr_t)&n;
	if (g_pmcalls++ < 400) {
		plog("stub %d (+0x%x) args %08x %08x %08x %08x %08x %08x %08x %08x", n, n * 4, a[0], a[1], a[2], a[3],
		     a[4], a[5], a[6], a[7]);
		for (int i = 0; i < 6; i++)
			if (readable(a[i], 12))
				plog(" | a%d->(%g %g %g)", i, ((float *)a[i])[0], ((float *)a[i])[1], ((float *)a[i])[2]);
		plog("\n");
	}
	bool a0stack = a[0] > here - 0x20000 && a[0] < here + 0x20000;
	if (a0stack && readable(a[1], 12) && readable(a[4], 12)) {
		trace_t *t = (trace_t *)a[0];
		memset(t, 0, sizeof(*t));
		t->fraction = 1.0f;
		memcpy(t->endpos, (void *)a[4], 12);
		return a[0];
	}
	return 0;
}

template <int N>
static uint32_t __cdecl pmstub(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                               uint32_t a6, uint32_t a7)
{
	uint32_t a[8] = {a0, a1, a2, a3, a4, a5, a6, a7};
	return stub_common(N, a);
}

typedef uint32_t(__cdecl *pmstub_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
template <int... I>
static const pmstub_t *make_stubs(std::integer_sequence<int, I...>)
{
	static const pmstub_t t[] = {&pmstub<I>...};
	return t;
}
static const pmstub_t *g_stubs = make_stubs(std::make_integer_sequence<int, PM_BLOCK / 4>{});

// Phase 2 (mode 8): Quake II-style callbacks at the offsets found in phase 1, everything else a
// known fill byte; the bytes Pmove changes are the outputs.
static int g_trace_ofs = -1, g_pc_ofs = -1;
static trace_t __cdecl pm_trace(const float *start, const float *mins, const float *maxs, const float *end)
{
	trace_t t;
	gi.trace(start, mins, maxs, end, g_pmself, 0x02010003, &t);
	return t;
}
static int __cdecl pm_pointcontents(const float *p) { return gi.pointcontents(p); }

static uint8_t g_lastcmd[64];
static int g_pmphase;
static void ClientThink(edict_t *ent, usercmd_t *cmd)
{
	if (memcmp(g_lastcmd, cmd, 32)) {
		memcpy(g_lastcmd, cmd, 32);
		hexdump("cmd", cmd, 32);
	}
	uint8_t *c = cmd ? (uint8_t *)cmd : NULL;
	if (!(g_mode & 12) || !c || (!(g_mode & 8) && *(short *)(c + 10) == 0) || ((g_mode & 8) && !c[0]))
		return;
	g_pmself = ent;
	if (g_mode & 4) {
		if (g_pmphase++)
			return;
		for (int i = 0; i < PM_BLOCK / 4; i++)
			*(uint32_t *)(g_pm + 4 * i) = (uint32_t)(uintptr_t)g_stubs[i];
		memset(g_pm, 0, 28);
		*(short *)(g_pm + 4) = (short)(g_spawn[0] * 8);
		*(short *)(g_pm + 6) = (short)(g_spawn[1] * 8);
		*(short *)(g_pm + 8) = (short)((g_spawn[2] + 10) * 8);
		*(short *)(g_pm + 0x12) = 800;
		memcpy(g_pm + 28, cmd, 24);
		plog("Pmove phase 1 call\n");
		gi.Pmove(g_pm);
		plog("Pmove phase 1 returned, %d stub calls\n", g_pmcalls);
		hexdump("pm after", g_pm, 512);
		for (int i = 13; i < PM_BLOCK / 4; i++)
			if (*(uint32_t *)(g_pm + 4 * i) != (uint32_t)(uintptr_t)g_stubs[i])
				plog("  changed +0x%x: %08x (%g)\n", 4 * i, *(uint32_t *)(g_pm + 4 * i), *(float *)(g_pm + 4 * i));
		return;
	}
	// phase 2: real trace/pointcontents, stubs from 0xF8 on, state carried between commands
	if (g_pmphase >= 60)
		return;
	g_trace_ofs = 0xF0;
	g_pc_ofs = 0xF4;
	static bool init;
	if (!init) {
		init = true;
		memset(g_pm, 0, 28);
		*(short *)(g_pm + 4) = (short)(g_spawn[0] * 8);
		*(short *)(g_pm + 6) = (short)(g_spawn[1] * 8);
		*(short *)(g_pm + 8) = (short)((g_spawn[2] + 10) * 8);
		*(short *)(g_pm + 0x12) = 800;
	}
	memset(g_pm + 28, 0, PM_BLOCK - 28);
	*(uint32_t *)(g_pm + 0xF8) = (uint32_t)(uintptr_t)g_stubs[0xF8 / 4];
	g_pm[0x1A] = (uint8_t)gi.cvar_value("probe_pmextra");
	uint32_t ovr = (uint32_t)gi.cvar_value("probe_pmword");
	if (ovr)
		*(uint32_t *)(g_pm + ovr) = 0;
	memcpy(g_pm + 28, cmd, 24);
	int fofs = (int)gi.cvar_value("probe_pmfloatofs");
	if (fofs >= 0xFC)
		*(float *)(g_pm + fofs) = 2.0f;
	int iofs = (int)gi.cvar_value("probe_pmintofs");
	if (iofs >= 0xFC)
		*(int *)(g_pm + iofs) = (int)gi.cvar_value("probe_pmintval");
	*(void **)(g_pm + g_trace_ofs) = (void *)pm_trace;
	*(void **)(g_pm + g_pc_ofs) = (void *)pm_pointcontents;
	int calls0 = g_pmcalls;
	static uint8_t before2[PM_BLOCK];
	memcpy(before2, g_pm, PM_BLOCK);
	gi.Pmove(g_pm);
	short *o = (short *)(g_pm + 4), *v = (short *)(g_pm + 10);
	plog("Pmove p2 #%d lean %d fwd %d: org %g %g %g vel %g %g %g flags %02x time %d numtouch %d vh %g ground %p wl %d stubs %d\n",
	     g_pmphase, (signed char)c[3], *(short *)(c + 10), o[0] / 8.0, o[1] / 8.0, o[2] / 8.0, v[0] / 8.0, v[1] / 8.0,
	     v[2] / 8.0, g_pm[16], g_pm[17], *(int *)(g_pm + 0x38), *(float *)(g_pm + 0xC8), *(void **)(g_pm + 0xE4),
	     *(int *)(g_pm + 0xEC), g_pmcalls - calls0);
	hexdump("  state", g_pm, 28);
	for (int i = 28 / 4; i < 1024 / 4; i++)
		if (i != 0x38 / 4 && (i < 0x3C / 4 || i >= 0xBC / 4) && i != 0xC8 / 4 && (i < 0xCC / 4 || i > 0xE4 / 4) &&
		    *(uint32_t *)(g_pm + 4 * i) != *(uint32_t *)(before2 + 4 * i))
			plog("  out +0x%03x: %08x int %d float %g\n", 4 * i, *(uint32_t *)(g_pm + 4 * i),
			     *(int *)(g_pm + 4 * i), *(float *)(g_pm + 4 * i));
	g_pmphase++;
}

static void ClientDropped(edict_t *) { plog("ClientDropped\n"); }
static qboolean SaveAllowed(void) { return 0; }
static void Nop(void) {}
static const char *GetGameType(void) { return "probe"; }
static int GetCinematicState(void) { return 0; }
static void SetCinematicState(int) {}
static void WriteGame(qboolean) {}
static qboolean ReadGame(int) { return 0; }

// Probe schedule: each pattern is held for 3 frames. word w of target t (0 = player state 200 bytes
// = 50 words, 1 = client's own entity state words 1..28), sub 0 = int 0x44332211, 1 = float 1234.5.
static float RunFrame(int framenum)
{
	uint8_t *cl = g_clients;
	uint8_t *ce = EDICT(1);
	float code = framenum * 0.1f;
	if (I(ce, 0x78)) {
		if (g_started < 0)
			g_started = framenum + 20; // let the client settle first
		int k = (framenum - g_started) / 3;
		int words_ps = 50, words_es = 28;
		memset(cl, 0, 200);
		memset(ce + 4, 0, 112);
		if (g_mode & 2)
			k += words_ps * 2;
		if (k >= 0 && k < (words_ps + words_es) * 2) {
			g_sub = k & 1;
			int w = k >> 1;
			if (w < words_ps) {
				g_target = 0;
				g_word = w;
				if (g_word != (int)gi.cvar_value("probe_skipps"))
					*(uint32_t *)(cl + 4 * g_word) = g_sub ? 0x449A5000u : 0x44332211u;
			} else {
				g_target = 1;
				g_word = w - words_ps + 1;
				// companions so that the probed word is put on the wire: a model makes the entity
				// a candidate, event 1 makes events present, a sound index sends the sound data
				I(ce, 0x20) = 3;
				if (g_word >= 0x48 / 4 && g_word <= 0x58 / 4)
					I(ce, 0x44) = 5;
				if (g_word == 0x40 / 4)
					I(ce, 0x3C) = 5;
				*(uint32_t *)(ce + 4 * g_word) = g_sub ? 0x449A5000u : 0x44332211u;
			}
			code = 1000.0f + g_target * 200 + g_word * 2 + g_sub;
			if ((framenum - g_started) % 3 == 0)
				plog("frame %d target %d word %d sub %d code %g\n", framenum, g_target, g_word, g_sub, code);
		} else {
			g_word = -1;
		}
	}
	return code;
}

extern "C" __declspec(dllexport) game_export_t *GetGameAPI(game_import_t *import)
{
	gi = *import;
	ge.apiversion = GAME_API_VERSION;
	ge.Init = Init;
	ge.Shutdown = Shutdown;
	ge.SpawnEntities = SpawnEntities;
	ge.WriteGame = WriteGame;
	ge.ReadGame = ReadGame;
	ge.WriteLevel = Nop;
	ge.ReadLevel = Nop;
	ge.ClientPreConnect = ClientPreConnect;
	ge.ClientConnect = ClientConnect;
	ge.ClientBegin = ClientBegin;
	ge.ClientUserinfoChanged = ClientUserinfoChanged;
	ge.ClientDisconnect = ClientDisconnect;
	ge.ClientCommand = ClientCommand;
	ge.ClientThink = ClientThink;
	ge.ClientDropped = ClientDropped;
	ge.SaveAllowed = SaveAllowed;
	ge.SavesLeft = Nop;
	ge.GameStats = Nop;
	ge.UpdateInven = Nop;
	ge.GetGameType = GetGameType;
	ge.GetCinematicState = GetCinematicState;
	ge.SetCinematicState = SetCinematicState;
	ge.RunFrame = RunFrame;
	ge.edicts = (edict_t *)g_edicts;
	ge.edict_size = EDICT_SIZE;
	ge.num_edicts = 1;
	ge.max_edicts = MAX_EDICTS;
	return &ge;
}
