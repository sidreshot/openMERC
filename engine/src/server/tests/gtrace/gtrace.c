/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc I1b - tracing proxy game module (test tool, not shipped). Installed as <game dir>\gamex86.dll
 * (e.g. D:\Games\SoF-test-I1b\i1bt with `+set game i1bt`), it loads the game module under test
 * (env I1B_GTRACE_TARGET, default <proxy dir>\..\base\gamex86.dll), hands it a copy of the engine's import
 * table in which the filtered trace (SPEC-001 #53, SPEC-313 s7) is wrapped, and returns the module's export
 * table unchanged. Every filtered trace that offers at least one entity to the module's callback is logged
 * (env I1B_GTRACE_LOG, default <proxy dir>\gtrace.log): the call, each candidate the engine offers (entity
 * number, fraction, the engine-visible fields of the candidate trace and entity) and the callback's answer,
 * then the result. The same proxy runs under the original engine and under OpenMerc, so both engines' offers
 * can be compared for the same game module. Only the SPEC-001 interface is exercised; no code of either
 * engine or of the game module is read.
 *
 * Implements: SPEC-001 (GetGameAPI, import #53), SPEC-313 s7 (filtered trace, observed from the module side)
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../common/header/sof/game_api.h"

static sof_game_import_t g_imp;
static sof_game_export_t *g_ge;
static FILE *g_log;
static int (*g_realfilter)(const float *, const float *, const float *, const float *, sof_edict_t *, int,
		sof_trace_t *, sof_tracefilter_t);
static sof_tracefilter_t g_userfilter;
static unsigned g_calls, g_logged;
static char g_dir[MAX_PATH];

/* imports the module calls while the engine is inside its filter callback (all function entries of the
   table pass through these thunks; x86 naked functions, eax is free at a cdecl call) */
static void *g_real[100];
static volatile int g_incb;
static volatile unsigned g_cblist[64];
static volatile unsigned g_cbcount;

#define GT_THUNK(n) \
	static __declspec(naked) void gt_thunk_##n(void) \
	{ \
		__asm cmp dword ptr g_incb, 0 \
		__asm je gt_skip_##n \
		__asm mov eax, g_cbcount \
		__asm cmp eax, 64 \
		__asm jae gt_count_##n \
		__asm mov dword ptr g_cblist[eax * 4], n \
		__asm gt_count_##n: \
		__asm inc dword ptr g_cbcount \
		__asm gt_skip_##n: \
		__asm jmp dword ptr g_real[n * 4] \
	}
#define GT_T10(a) GT_THUNK(a##0) GT_THUNK(a##1) GT_THUNK(a##2) GT_THUNK(a##3) GT_THUNK(a##4) GT_THUNK(a##5) \
	GT_THUNK(a##6) GT_THUNK(a##7) GT_THUNK(a##8) GT_THUNK(a##9)
GT_THUNK(0) GT_THUNK(1) GT_THUNK(2) GT_THUNK(3) GT_THUNK(4) GT_THUNK(5) GT_THUNK(6) GT_THUNK(7) GT_THUNK(8)
GT_THUNK(9)
GT_T10(1) GT_T10(2) GT_T10(3) GT_T10(4) GT_T10(5) GT_T10(6) GT_T10(7) GT_T10(8) GT_T10(9)
#define GT_P10(a) gt_thunk_##a##0, gt_thunk_##a##1, gt_thunk_##a##2, gt_thunk_##a##3, gt_thunk_##a##4, \
	gt_thunk_##a##5, gt_thunk_##a##6, gt_thunk_##a##7, gt_thunk_##a##8, gt_thunk_##a##9
static void (*const g_thunks[100])(void) = {
	gt_thunk_0, gt_thunk_1, gt_thunk_2, gt_thunk_3, gt_thunk_4, gt_thunk_5, gt_thunk_6, gt_thunk_7, gt_thunk_8,
	gt_thunk_9, GT_P10(1), GT_P10(2), GT_P10(3), GT_P10(4), GT_P10(5), GT_P10(6), GT_P10(7), GT_P10(8), GT_P10(9)
};

static int
EntNum(const sof_edict_t *e)
{
	if (!e)
	{
		return -1;
	}

	if (!g_ge || !g_ge->edicts || (g_ge->edict_size <= 0))
	{
		return -2;
	}

	return (int)(((const char *)e - (const char *)g_ge->edicts) / g_ge->edict_size);
}

static void
LogTrace(const char *what, const sof_trace_t *t)
{
	const sof_edict_t *e = t->ent;

	fprintf(g_log, "  %s ent %d frac %.4f allsolid %d startsolid %d contents %08x surface %s flags %08x "
			"leaf %d normal (%.2f %.2f %.2f)", what, EntNum(e), t->fraction, t->allsolid, t->startsolid,
			t->contents, t->surface ? "set" : "NULL", t->surface ? t->surface->flags : 0, t->leafnum,
			t->plane.normal[0], t->plane.normal[1], t->plane.normal[2]);

	if (e)
	{
		fprintf(g_log, " | solid %d svflags %x model %d rfx %x client %s ghoul %s owner %d inuse %d",
				e->solid, e->svflags, e->s.modelindex, e->s.renderfx, e->client ? "set" : "NULL",
				e->ghoulInst ? "set" : "NULL", EntNum(e->owner), e->inuse);

		if (e->client)
		{
			const sof_player_state_t *ps = (const sof_player_state_t *)e->client;

			fprintf(g_log, " | ps.gun %s type %d pm_type %d remote %d", ps->gun ? "set" : "NULL", ps->gun_type,
					ps->pmove.pm_type, ps->remote_entity);
		}
	}

	fprintf(g_log, "\n");
}

/* the call line is printed only for calls that offer an entity or hit one */
static char g_pending[512];

static void
FlushPending(void)
{
	if (g_pending[0])
	{
		fputs(g_pending, g_log);
		g_pending[0] = 0;
	}
}

/* words at an address the engine passed (read with a guard: it may not be a pointer) */
static void
DumpWords(const char *name, const void *p, int count)
{
	const unsigned *w = (const unsigned *)p;
	int i;

	if (!p || IsBadReadPtr(p, count * 4))
	{
		fprintf(g_log, "    %s: not readable\n", name);
		return;
	}

	fprintf(g_log, "    %s:", name);

	for (i = 0; i < count; i++)
	{
		float f;

		memcpy(&f, &w[i], 4);

		if ((w[i] != 0) && (f > -100000.0f) && (f < 100000.0f) && ((f > 0.001f) || (f < -0.001f)))
		{
			fprintf(g_log, " [%d]%08x(%.3f)", i, w[i], f);
		}
		else
		{
			fprintf(g_log, " [%d]%08x", i, w[i]);
		}
	}

	fprintf(g_log, "\n");
}

/* The filter is called as cdecl with the candidate; the words after it on the stack are logged as well (and
   passed on) to see whether either side uses more arguments than SPEC-313 s7 lists. Extra cdecl arguments are
   harmless: the caller removes them. */
typedef int (*filter3_t)(sof_trace_t *tr, void *a2, void *a3);
typedef int (*tracefilter10_t)(const float *, const float *, const float *, const float *, sof_edict_t *, int,
		sof_trace_t *, void *, void *, void *);
static void *g_extra9, *g_extra10;

static int
FilterWrap(sof_trace_t *tr, void *a2, void *a3)
{
	int r;

	FlushPending();
	LogTrace("offer", tr);
	fprintf(g_log, "    candidate %p, callback args 2,3: %p %p (tracefilter args 9,10: %p %p)\n", (void *)tr, a2, a3,
			g_extra9, g_extra10);
	DumpWords("arg2", a2, 40);
	DumpWords("arg3", a3, 24);
	fflush(g_log);
	g_cbcount = 0;
	g_incb++;
	r = ((filter3_t)(void *)g_userfilter)(tr, a2, a3); /* as received */
	g_incb--;
	fprintf(g_log, "    -> %s", r ? "accepted" : "refused");

	if (g_cbcount)
	{
		unsigned i;

		fprintf(g_log, "; imports inside the callback:");

		for (i = 0; i < g_cbcount && i < 64; i++)
		{
			fprintf(g_log, " #%u", g_cblist[i]);
		}
	}

	fprintf(g_log, "\n");
	return r;
}

static int
TraceFilterWrap(const float *start, const float *mins, const float *maxs, const float *end, sof_edict_t *passent,
		int mask, sof_trace_t *out, sof_tracefilter_t filter, void *extra9, void *extra10)
{
	sof_tracefilter_t saved = g_userfilter;
	void *s9 = g_extra9, *s10 = g_extra10;
	int r;

	g_calls++;
	_snprintf(g_pending, sizeof(g_pending) - 1,
			"tracefilter #%u passent %d mask %08x %s (%.3f %.3f %.3f)->(%.3f %.3f %.3f) filter %s args 9,10 %p %p "
			"start %p end %p out %p\n",
			g_calls, EntNum(passent), mask, (mins || maxs) ? "box" : "point", start[0], start[1], start[2], end[0],
			end[1], end[2], filter ? "set" : "NULL", extra9, extra10, (void *)start, (void *)end, (void *)out);
	g_userfilter = filter;
	g_extra9 = extra9;
	g_extra10 = extra10;
	r = ((tracefilter10_t)(void *)g_realfilter)(start, mins, maxs, end, passent, mask, out,
			filter ? (void *)FilterWrap : NULL, extra9, extra10);
	g_userfilter = saved;
	g_extra9 = s9;
	g_extra10 = s10;

	if (g_pending[0] && (EntNum(out->ent) > 0))
	{
		FlushPending();
	}

	if (!g_pending[0])
	{
		LogTrace("result", out);
		fflush(g_log);
		g_logged++;
	}

	g_pending[0] = 0;
	return r;
}


/* timeline of the level start (env I1B_GTRACE_TIMELINE=1): the engine's calls of ClientBegin, ClientThink and
   RunFrame with the cinematic state the module reports (export 0x54), for the first frames of each level */
static sof_game_export_t g_ex, *g_realex;
static int g_timeline, g_thinks, g_lastcin = -1, g_frames;

static int
CinState(void)
{
	return g_realex->GetCinematicState ? (g_realex->GetCinematicState() & 0xff) : -1;
}

static void
ExSpawnEntities(const char *map, const char *ents, const char *spawn)
{
	fprintf(g_log, "TL SpawnEntities %s\n", map);
	g_frames = 0;
	g_thinks = 0;
	g_lastcin = -1;
	g_realex->SpawnEntities(map, ents, spawn);
}

static void
ExClientBegin(sof_edict_t *ent)
{
	fprintf(g_log, "TL ClientBegin ent %d (frames so far %d) cin before %d\n", EntNum(ent), g_frames, CinState());
	g_realex->ClientBegin(ent);
	fprintf(g_log, "TL   after ClientBegin cin %d\n", CinState());
	fflush(g_log);
}

static void
ExClientThink(sof_edict_t *ent, sof_usercmd_t *cmd)
{
	if (g_thinks++ < 60)
	{
		fprintf(g_log, "TL ClientThink ent %d msec %d buttons %x cin %d (frames %d)\n", EntNum(ent), cmd->msec,
				cmd->buttons, CinState(), g_frames);
	}

	g_realex->ClientThink(ent, cmd);
}

static float
ExRunFrame(int counter)
{
	float t = g_realex->RunFrame(counter);
	int cin = CinState();

	g_frames++;

	if ((cin != g_lastcin) || (g_frames <= 8))
	{
		fprintf(g_log, "TL RunFrame %d -> %.2f cin %d\n", counter, t, cin);
		g_lastcin = cin;
		fflush(g_log);
	}

	return t;
}

__declspec(dllexport) sof_game_export_t *
GetGameAPI(sof_game_import_t *import)
{
	char target[MAX_PATH], logpath[MAX_PATH];
	HMODULE self = NULL, real;
	sof_getgameapi_t api;
	char *slash;
	DWORD n;

	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)(void *)&GetGameAPI, &self);
	GetModuleFileNameA(self, g_dir, sizeof(g_dir));
	slash = strrchr(g_dir, '\\');

	if (slash)
	{
		*slash = 0;
	}

	n = GetEnvironmentVariableA("I1B_GTRACE_LOG", logpath, sizeof(logpath));

	if (!n || (n >= sizeof(logpath)))
	{
		_snprintf(logpath, sizeof(logpath), "%s\\gtrace.log", g_dir);
	}

	if (!g_log)
	{
		g_log = fopen(logpath, "w");
	}

	if (!g_log)
	{
		return NULL;
	}

	n = GetEnvironmentVariableA("I1B_GTRACE_TARGET", target, sizeof(target));

	if (!n || (n >= sizeof(target)))
	{
		_snprintf(target, sizeof(target), "%s\\..\\base\\gamex86.dll", g_dir);
	}

	real = LoadLibraryA(target);
	api = real ? (sof_getgameapi_t)(void *)GetProcAddress(real, "GetGameAPI") : NULL;
	fprintf(g_log, "gtrace: target %s %s\n", target, api ? "loaded" : "NOT LOADED");
	fflush(g_log);

	if (!api)
	{
		return NULL;
	}

	/* env I1B_GTRACE_SP=1: single-player rules on a dedicated server, which forces deathmatch 1 before it
	   loads the module (import #90, as G2's spshim) */
	n = GetEnvironmentVariableA("I1B_GTRACE_SP", logpath, sizeof(logpath));

	if (n && (logpath[0] == '1'))
	{
		import->cvar_forceset("deathmatch", "0");
		fprintf(g_log, "gtrace: deathmatch forced to 0\n");
	}

	g_imp = *import;

	{
		void **e = (void **)&g_imp;
		int i;

		for (i = 0; i < 99; i++)
		{
			if ((i == 24) || (i == 53) || (i == 67) || !e[i])
			{
				continue;
			}

			g_real[i] = e[i];
			e[i] = (void *)g_thunks[i];
		}
	}

	g_realfilter = g_imp.tracefilter;
	g_imp.tracefilter = (void *)TraceFilterWrap;
	g_ge = api(&g_imp);

	n = GetEnvironmentVariableA("I1B_GTRACE_TIMELINE", logpath, sizeof(logpath));

	if (g_ge && n && (logpath[0] == '1'))
	{
		/* the engine reads edicts/num_edicts from the table it gets: hand it the module's own table with the
		   function entries replaced (the data fields are refreshed by the module through its own copy, so the
		   wrapped table must be the module's - patch it in place) */
		g_realex = &g_ex;
		g_ex = *g_ge;
		g_ge->SpawnEntities = ExSpawnEntities;
		g_ge->ClientBegin = ExClientBegin;
		g_ge->ClientThink = ExClientThink;
		g_ge->RunFrame = ExRunFrame;
		fprintf(g_log, "gtrace: timeline on\n");
	}

	return g_ge;
}
