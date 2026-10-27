/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenMerc sound module: engine services and the client data the engine shares with us.
 *
 * The engine functions are looked up by name in the executable that loaded us, so the module
 * works whatever the executable is called and does not link against it.
 *
 * Implements: SPEC-004 ("Engine functions imported by name", "The sound struct"),
 *             SPEC-AMEND-001 A (argument lists), SPEC-040 3.1 (16-bit ambient field),
 *             SPEC-300 (FS_LoadFile / FS_FreeFile), SPEC-002 (cvar object layout)
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
/* Linux and other ELF systems: the engine executable exports the same names through its dynamic symbol
   table (engine/src/common/sof_exports.dynlist); the ones whose module-facing argument list differs from
   the engine's own function are exported under their SoFExp_ wrapper names. */
#include <dlfcn.h>
#include <time.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "snd_local.h"

engine_t eng;

/* the five pointers of S_SetSoundStruct */
static byte *ss_entring;   /* #0 ring of 1024 parsed entity states, 116-byte records */
static byte *ss_cl;        /* #1 client state */
static byte *ss_entities;  /* #2 1024 client entities, 396-byte records */
static byte *ss_cls;       /* #3 client static state */
static void *ss_hwnd;      /* #4 HWND * */

#define ENTSTATE_SIZE 116
#define CLENTITY_SIZE 396

void Snd_DebugString(const char *s)
{
#ifdef _WIN32
    OutputDebugStringA(s);
#else
    fputs(s, stderr);
#endif
}

#ifdef _WIN32
#define ENG_LOOKUP(winname, elfname) ((void *)GetProcAddress(exe, winname))
#else
#define ENG_LOOKUP(winname, elfname) dlsym(RTLD_DEFAULT, elfname)
#endif

qboolean Eng_Resolve(void)
{
#ifdef _WIN32
    HMODULE exe = GetModuleHandleA(NULL);
#endif
    qboolean ok = true;

    memset(&eng, 0, sizeof(eng));
#define IMPORT2(field, name, elfname, mandatory)                                        \
    do {                                                                               \
        void *p_ = ENG_LOOKUP(name, elfname);                                          \
        memcpy(&eng.field, &p_, sizeof(p_));                                           \
        if (!eng.field && mandatory) {                                                 \
            char m[128];                                                               \
            _snprintf_s(m, sizeof(m), _TRUNCATE, "OMsnd: engine export %s missing\n", name); \
            Snd_DebugString(m);                                                        \
            ok = false;                                                                \
        }                                                                              \
    } while (0)
#define IMPORT(field, name, mandatory) IMPORT2(field, name, name, mandatory)
    IMPORT(Com_Printf, "Com_Printf", true);
    IMPORT(Com_DPrintf, "Com_DPrintf", false);
    IMPORT(Com_Error, "Com_Error", true);
    IMPORT2(Cvar_Get, "Cvar_Get", "SoFExp_Cvar_Get", true);
    IMPORT(Cmd_AddCommand, "Cmd_AddCommand", true);
    IMPORT(Cmd_RemoveCommand, "Cmd_RemoveCommand", true);
    IMPORT(Cmd_Argc, "Cmd_Argc", true);
    IMPORT(Cmd_Argv, "Cmd_Argv", true);
    IMPORT2(FS_LoadFile, "FS_LoadFile", "SoFExp_FS_LoadFile", true);
    IMPORT(FS_FreeFile, "FS_FreeFile", true);
    IMPORT2(FS_FOpenFile, "FS_FOpenFile", "SoFExp_FS_FOpenFile", false);
    IMPORT2(FS_FCloseFile, "FS_FCloseFile", "SoFExp_FS_FCloseFile", false);
    IMPORT(irand, "irand", false);
    IMPORT(CL_GetEntitySoundOrigin, "CL_GetEntitySoundOrigin", false);
#undef IMPORT
#undef IMPORT2
    return ok;
}

void Eng_SetSoundStruct(void *const *ptrs)
{
    if (!ptrs) {
        ss_entring = ss_cl = ss_entities = ss_cls = NULL;
        ss_hwnd = NULL;
        return;
    }
    ss_entring = (byte *)ptrs[0];
    ss_cl = (byte *)ptrs[1];
    ss_entities = (byte *)ptrs[2];
    ss_cls = (byte *)ptrs[3];
    ss_hwnd = ptrs[4];
}

qboolean Eng_HaveStruct(void)
{
    return ss_entring && ss_cl && ss_entities && ss_cls;
}

/* the game window is minimised or hidden (sound struct #4); true when no window is known */
qboolean Eng_WindowHidden(void)
{
#ifndef _WIN32
    /* no native window handle in the sound struct outside Windows: never counted as hidden, so
       s_om_focusmute does not mute (docs/BUILD-linux.md, known differences) */
    return false;
#else
    HWND w = ss_hwnd ? *(HWND *)ss_hwnd : NULL;
    if (!w || !IsWindow(w))
        return true;
    return IsIconic(w) || !IsWindowVisible(w);
#endif
}

/* ---- printing ------------------------------------------------------------------- */

void Snd_Printf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (eng.Com_Printf)
        eng.Com_Printf("%s", buf);
    else
        Snd_DebugString(buf);
}

void Snd_DPrintf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    if (!eng.Com_DPrintf)
        return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    eng.Com_DPrintf("%s", buf);
}

/* ---- time and random -------------------------------------------------------------- */

void Snd_Sleep(unsigned ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

unsigned Snd_Milliseconds(void)
{
#ifndef _WIN32
    static struct timespec base;
    struct timespec now;
    if (!base.tv_sec && !base.tv_nsec)
        clock_gettime(CLOCK_MONOTONIC, &base);
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (unsigned)((now.tv_sec - base.tv_sec) * 1000LL + (now.tv_nsec - base.tv_nsec) / 1000000);
#else
    static LARGE_INTEGER freq, base;
    LARGE_INTEGER now;
    if (!freq.QuadPart) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&base);
    }
    QueryPerformanceCounter(&now);
    return (unsigned)(((now.QuadPart - base.QuadPart) * 1000) / freq.QuadPart);
#endif
}

/* SPEC-040 uses the engine's shared generator for the ambient draws (Defsnd imports irand); without it a
 * private xorshift generator gives the same distribution. */
int Snd_Rand(int min, int max)
{
    static unsigned state;
    if (max <= min)
        return min;
    if (eng.irand)
        return eng.irand(min, max);
    if (!state) {
#ifdef _WIN32
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        state = (unsigned)t.QuadPart | 1u;
#else
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        state = (unsigned)(t.tv_nsec ^ t.tv_sec) | 1u;
#endif
    }
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return min + (int)(state % (unsigned)(max - min + 1));
}

/* ---- files -------------------------------------------------------------------------- */

int Snd_LoadFile(const char *path, void **buf)
{
    if (buf)
        *buf = NULL;
    if (!eng.FS_LoadFile)
        return -1;
    /* SPEC-AMEND-001 A: the third argument is ignored */
    return eng.FS_LoadFile(path, buf, 0);
}

void Snd_FreeFile(void *buf)
{
    if (buf && eng.FS_FreeFile)
        eng.FS_FreeFile(buf);
}

/* existence test for model-specific player sounds: FS_FOpenFile/FS_FCloseFile (SPEC-AMEND-001 A: handle
 * 1..20, 0 = not opened), else a length-only FS_LoadFile */
qboolean Snd_FileExists(const char *path)
{
    if (eng.FS_FOpenFile && eng.FS_FCloseFile) {
        int h = 0;
        eng.FS_FOpenFile(path, &h);
        if (!h)
            return false;
        eng.FS_FCloseFile(h);
        return true;
    }
    return eng.FS_LoadFile && eng.FS_LoadFile(path, NULL, 0) >= 0;
}

/* ---- client data (SPEC-004 sound struct offsets) ------------------------------------ */

int CL_State(void)
{
    return ss_cls ? *(int *)(ss_cls + 0) : 0;
}

qboolean CL_ShuttingDown(void)
{
    return ss_cls ? ss_cls[0x24] != 0 : false;
}

qboolean CL_Prepped(void)
{
    return ss_cl ? ss_cl[0x0D] != 0 : false;
}

int CL_GeneralAmbientField(void)
{
    return ss_cl ? *(const unsigned short *)(ss_cl + 0x9A8) : 0;
}

int CL_MusicMode(void)
{
    return ss_cl ? ss_cl[0x9AA] : 0;
}

int CL_FrameNumEntities(void)
{
    return ss_cl ? *(int *)(ss_cl + 0x9D4) : 0;
}

int CL_FrameFirstEntity(void)
{
    return ss_cl ? *(int *)(ss_cl + 0x9D8) : 0;
}

int CL_Time(void)
{
    return ss_cl ? *(int *)(ss_cl + 0x1A3C) : 0;
}

int CL_PlayerNum(void)
{
    return ss_cl ? *(int *)(ss_cl + 0x1F3C) : 0;
}

const char *CL_ConfigString(int n)
{
    if (!ss_cl || n < 0)
        return "";
    return (const char *)(ss_cl + 0x1F44 + 64 * n);
}

sfx_t *CL_SoundPrecache(int i)
{
    if (!ss_cl || i < 0 || i >= OM_MAX_SOUNDS)
        return NULL;
    if (i >= MAX_SOUNDS)
        return ((sfx_t **)(ss_cl + OM_CL_SOUNDS_EXT))[i - MAX_SOUNDS];
    return ((sfx_t **)(ss_cl + 0x3A004))[i];
}

int CL_EntityCurrentNumber(int entnum)
{
    if (!ss_entities || entnum < 0 || entnum >= 1024)
        return entnum;
    return *(int *)(ss_entities + (size_t)entnum * CLENTITY_SIZE + 0x74);
}

qboolean CL_FrameEntity(int i, frame_entity_t *out)
{
    const byte *e;
    if (!ss_entring || !ss_cl)
        return false;
    e = ss_entring + (size_t)((CL_FrameFirstEntity() + i) & (MAX_PARSE_ENTITIES - 1)) * ENTSTATE_SIZE;
    out->number = *(const int *)(e + 0);
    memcpy(out->origin, e + 4, sizeof(vec3_t));
    out->flags = *(const int *)(e + 0x30);
    out->sound = *(const int *)(e + 0x3C);
    out->loopparm = e[0x40];
    return true;
}
