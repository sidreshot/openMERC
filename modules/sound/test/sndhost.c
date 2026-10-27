/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * sndhost: black-box test host for a SoF sound module (OMsnd.dll). It exports the engine functions the
 * module imports by name (SPEC-004, SPEC-AMEND-001 A), reads sounds from the game's .pak files, fills a
 * fake sound struct (in game, level prepped) and runs a timed script against the module's exports, logging
 * the QPC time of every action so a capture (s_om_wavefile or proccap) can be aligned with it.
 *
 * usage: sndhost <OMsnd.dll> <basedir> <script> [+set name value ...]
 * script lines: <ms> <action> [args]   (times since the start of the frame loop)
 *   start <sound> <ent> <chan> <vol> <attn> [x y z]  S_StartSound (origin "-" or absent: entity origin)
 *   local <sound>          S_StartLocalSound
 *   entorg <ent> x y z     origin returned by CL_GetEntitySoundOrigin
 *   (ambient also registers every sound/ambient/ wave of the paks, as a level's configstrings would)
 *   music <set>            S_RegisterMusicSet           musicmode <n>   player-state music byte
 *   ambient <file>         S_RegisterAmbientSet         general <n>     player-state general set
 *   loop <slot> <ent> <soundindex> <parm> x y z       frame entity with a loop sound (precache slot)
 *   precache <index> <sound>   cl sound precache entry
 *   playernum <n>          cl player number (the listener entity is n + 1)
 *   (any action may end in tokens for hostlevels.py, e.g. "expect=local"; sndhost ignores them)
 *   cvar <name> <value>    proctype <n>    stopall    activate <0|1>    paused <0|1>    mark <label>    quit
 *   listener x y z yawdeg
 * Implements: SPEC-004, SPEC-040 (black-box verification harness; no game code)
 */
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPORT __declspec(dllexport)

typedef struct cvar_s {
    char *name, *string, *latched_string;
    int flags;
    void (*changed)(struct cvar_s *);
    int modified;
    float value;
    struct cvar_s *next;
} cvar_t;

static cvar_t *cvars;
static FILE *hostlog;
static LARGE_INTEGER qfreq, q0;

static double now_ms(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - q0.QuadPart) * 1000.0 / (double)qfreq.QuadPart;
}

static void logline(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
    if (hostlog) {
        fputs(buf, hostlog);
        fflush(hostlog);
    }
}

EXPORT void Com_Printf(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    logline("%s", buf);
}
EXPORT void Com_DPrintf(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    logline("[d] %s", buf);
}
EXPORT void Com_Error(int code, const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    logline("ERROR %d: %s\n", code, buf);
    exit(3);
}

static cvar_t *find_cvar(const char *name)
{
    cvar_t *v;
    for (v = cvars; v; v = v->next)
        if (!_stricmp(v->name, name))
            return v;
    return NULL;
}
static void set_cvar(const char *name, const char *value)
{
    cvar_t *v = find_cvar(name);
    if (!v) {
        v = (cvar_t *)calloc(1, sizeof(*v));
        v->name = _strdup(name);
        v->next = cvars;
        cvars = v;
    } else
        free(v->string);
    v->string = _strdup(value);
    v->value = (float)atof(value);
    v->modified = 1;
}
EXPORT cvar_t *Cvar_Get(const char *name, const char *value, int flags, void (*changed)(cvar_t *))
{
    cvar_t *v = find_cvar(name);
    if (!v)
        set_cvar(name, value ? value : "");
    v = find_cvar(name);
    v->flags |= flags;
    return v;
}

#define MAXCMD 64
static struct {
    char name[64];
    void (*fn)(void);
} cmds[MAXCMD];
static int argc_;
static char argv_[16][256];
EXPORT void Cmd_AddCommand(const char *name, void (*fn)(void))
{
    int i;
    for (i = 0; i < MAXCMD; i++)
        if (!cmds[i].fn) {
            strncpy(cmds[i].name, name, 63);
            cmds[i].fn = fn;
            return;
        }
}
EXPORT void Cmd_RemoveCommand(const char *name)
{
    int i;
    for (i = 0; i < MAXCMD; i++)
        if (cmds[i].fn && !_stricmp(cmds[i].name, name))
            cmds[i].fn = NULL;
}
EXPORT int Cmd_Argc(void) { return argc_; }
EXPORT char *Cmd_Argv(int i) { return i < argc_ ? argv_[i] : ""; }

/* ---- files: loose files under basedir, then the .pak files ---- */
typedef struct {
    char name[56];
    int ofs, len, pak;
} pakent_t;
static pakent_t *pents;
static int npents;
static char pakpath[16][MAX_PATH];
static int npaks;
static char basedir[MAX_PATH];

static void load_paks(void)
{
    int i;
    for (i = 0; i < 16; i++) {
        char p[MAX_PATH];
        FILE *f;
        int hdr[3], n, k;
        sprintf(p, "%s\\pak%d.pak", basedir, i);
        f = fopen(p, "rb");
        if (!f)
            continue;
        fread(hdr, 4, 3, f);
        n = hdr[2] / 64;
        pents = (pakent_t *)realloc(pents, sizeof(pakent_t) * (npents + n));
        fseek(f, hdr[1], SEEK_SET);
        for (k = 0; k < n; k++) {
            char rec[64];
            fread(rec, 1, 64, f);
            memcpy(pents[npents].name, rec, 56);
            pents[npents].name[55] = 0;
            _strlwr(pents[npents].name);
            memcpy(&pents[npents].ofs, rec + 56, 4);
            memcpy(&pents[npents].len, rec + 60, 4);
            pents[npents].pak = npaks;
            npents++;
        }
        strcpy(pakpath[npaks++], p);
        fclose(f);
    }
}
static int fs_find(const char *path, FILE **pf, int *len)
{
    char lp[MAX_PATH], full[MAX_PATH];
    int i;
    FILE *f;
    sprintf(full, "%s\\%s", basedir, path);
    f = fopen(full, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        *len = ftell(f);
        fseek(f, 0, SEEK_SET);
        *pf = f;
        return 1;
    }
    strncpy(lp, path, sizeof(lp) - 1);
    lp[sizeof(lp) - 1] = 0;
    _strlwr(lp);
    for (i = 0; lp[i]; i++)
        if (lp[i] == '\\')
            lp[i] = '/';
    for (i = npents - 1; i >= 0; i--) /* later paks override */
        if (!strcmp(pents[i].name, lp)) {
            f = fopen(pakpath[pents[i].pak], "rb");
            if (!f)
                return 0;
            fseek(f, pents[i].ofs, SEEK_SET);
            *len = pents[i].len;
            *pf = f;
            return 1;
        }
    return 0;
}
EXPORT int FS_LoadFile(const char *path, void **buf, int unused)
{
    FILE *f;
    int len;
    if (buf)
        *buf = NULL;
    if (!fs_find(path, &f, &len))
        return -1;
    if (buf) {
        *buf = malloc((size_t)len + 1);
        fread(*buf, 1, (size_t)len, f);
    }
    fclose(f);
    return len;
}
EXPORT void FS_FreeFile(void *buf) { free(buf); }
EXPORT int FS_FOpenFile(const char *name, int *handle)
{
    FILE *f;
    int len;
    *handle = 0;
    if (!fs_find(name, &f, &len))
        return -1;
    fclose(f);
    *handle = 1;
    return len;
}
EXPORT void FS_FCloseFile(int h) {}
EXPORT int irand(int min, int max) { return max <= min ? min : min + rand() % (max - min + 1); }

static float entorg[1024][3];
EXPORT void CL_GetEntitySoundOrigin(int ent, float *out)
{
    out[0] = entorg[ent][0];
    out[1] = entorg[ent][1];
    out[2] = entorg[ent][2];
}

/* ---- fake client data (SPEC-004 sound struct offsets) ---- */
static unsigned char *entring, *cl, *entities, *cls;
static void *hwnd;

typedef void *(*fn_RegisterSound)(const char *);
static struct {
    void (*Init)(void);
    void (*Shutdown)(void);
    void (*StartSound)(const float *, int, int, void *, float, float, float);
    void (*StartLocalSound)(const char *);
    void (*StopAllSounds)(void);
    void (*Update)(const float *, const float *, const float *, const float *);
    void (*Activate)(int);
    void (*BeginRegistration)(void);
    fn_RegisterSound RegisterSound;
    void (*EndRegistration)(void);
    void (*RegisterMusicSet)(const char *);
    void (*RegisterAmbientSet)(const char *);
    void (*SetSoundProcType)(unsigned char);
    void (*SetSoundStruct)(void *const *);
} S;

#define MAXLOOPS 64
static struct {
    int used, ent, sound, parm;
    float org[3];
} loops[MAXLOOPS];

static void build_frame(void)
{
    int i, n = 0;
    for (i = 0; i < MAXLOOPS; i++)
        if (loops[i].used) {
            unsigned char *e = entring + n * 116;
            memset(e, 0, 116);
            *(int *)(e + 0) = loops[i].ent;
            memcpy(e + 4, loops[i].org, 12);
            *(int *)(e + 0x3C) = loops[i].sound;
            e[0x40] = (unsigned char)loops[i].parm;
            n++;
        }
    *(int *)(cl + 0x9D4) = n;
    *(int *)(cl + 0x9D8) = 0;
}

int main(int argc, char **argv)
{
    HMODULE m;
    FILE *sf;
    char line[512];
    void *ptrs[5];
    float lorg[3] = {0, 0, 0}, yaw = 0;
    int i;
    struct {
        int ms;
        char text[480];
    } *acts = NULL;
    int nacts = 0, next = 0;
    double start;
    char logpath[MAX_PATH];

    if (argc < 4) {
        fprintf(stderr, "usage: sndhost <OMsnd.dll> <basedir> <script> [+set name value ...]\n");
        return 1;
    }
    QueryPerformanceFrequency(&qfreq);
    QueryPerformanceCounter(&q0);
    timeBeginPeriod(1);
    strcpy(basedir, argv[2]);
    sprintf(logpath, "%s.log", argv[3]);
    hostlog = fopen(logpath, "w");
    logline("qpc0_100ns %lld\n", (long long)(q0.QuadPart * 10000000.0 / qfreq.QuadPart));
    for (i = 4; i + 2 < argc; i++)
        if (!strcmp(argv[i], "+set")) {
            set_cvar(argv[i + 1], argv[i + 2]);
            i += 2;
        }
    load_paks();
    logline("paks %d, entries %d\n", npaks, npents);

    m = LoadLibraryA(argv[1]);
    if (!m) {
        logline("cannot load %s (%lu)\n", argv[1], GetLastError());
        return 2;
    }
#define GET(f, n) *(FARPROC *)&S.f = GetProcAddress(m, n)
    GET(Init, "S_Init"); GET(Shutdown, "S_Shutdown"); GET(StartSound, "S_StartSound");
    GET(StartLocalSound, "S_StartLocalSound"); GET(StopAllSounds, "S_StopAllSounds"); GET(Update, "S_Update");
    GET(Activate, "S_Activate"); GET(BeginRegistration, "S_BeginRegistration"); GET(RegisterSound, "S_RegisterSound");
    GET(EndRegistration, "S_EndRegistration"); GET(RegisterMusicSet, "S_RegisterMusicSet");
    GET(RegisterAmbientSet, "S_RegisterAmbientSet"); GET(SetSoundProcType, "S_SetSoundProcType");
    GET(SetSoundStruct, "S_SetSoundStruct");

    entring = (unsigned char *)calloc(1024, 116);
    cl = (unsigned char *)calloc(1, 0x3A004 + 4 * 400);
    entities = (unsigned char *)calloc(1024, 396);
    cls = (unsigned char *)calloc(1, 0x100);
    for (i = 0; i < 1024; i++)
        *(int *)(entities + i * 396 + 0x74) = i;
    *(int *)(cls + 0) = 8;            /* CA_ACTIVE */
    cl[0x0D] = 1;                     /* prepped */
    *(int *)(cl + 0x1F3C) = 0;        /* player number 0: entity 1 */
    cl[0x9AA] = 1;                    /* music mode 1 (silence) */
    ptrs[0] = entring; ptrs[1] = cl; ptrs[2] = entities; ptrs[3] = cls; ptrs[4] = &hwnd;
    S.SetSoundStruct(ptrs);
    S.Init();

    sf = fopen(argv[3], "r");
    if (!sf) {
        logline("no script %s\n", argv[3]);
        return 2;
    }
    while (fgets(line, sizeof(line), sf)) {
        int ms;
        char rest[480];
        if (line[0] == '#' || sscanf(line, "%d %479[^\n]", &ms, rest) != 2)
            continue;
        acts = realloc(acts, sizeof(*acts) * (nacts + 1));
        acts[nacts].ms = ms;
        strcpy(acts[nacts].text, rest);
        nacts++;
    }
    fclose(sf);

    S.BeginRegistration();
    S.EndRegistration();
    start = now_ms();
    logline("loop_start_ms %.3f\n", start);
    for (;;) {
        double t = now_ms() - start;
        float fwd[3], right[3], up[3] = {0, 0, 1};
        float yr = yaw * 3.14159265f / 180.0f;
        while (next < nacts && acts[next].ms <= t) {
            char a[64], s1[256], s2[256];
            float x, y, z, f1, f2;
            int i1, i2, i3, i4, nf;
            const char *txt = acts[next].text;
            sscanf(txt, "%63s", a);
            logline("act %.3f qpcms %.3f %s\n", t, now_ms(), txt);
            if (!strcmp(a, "start")) {
                nf = sscanf(txt, "%*s %255s %d %d %f %f %f %f %f", s1, &i1, &i2, &f1, &f2, &x, &y, &z);
                {
                    void *sfx = S.RegisterSound(s1);
                    float o[3] = {x, y, z};
                    S.StartSound(nf >= 8 ? o : NULL, i1, i2, sfx, f1, f2, 0);
                }
            } else if (!strcmp(a, "local")) {
                sscanf(txt, "%*s %255s", s1);
                S.StartLocalSound(s1);
            } else if (!strcmp(a, "entorg")) {
                sscanf(txt, "%*s %d %f %f %f", &i1, &x, &y, &z);
                entorg[i1][0] = x; entorg[i1][1] = y; entorg[i1][2] = z;
            } else if (!strcmp(a, "music")) {
                sscanf(txt, "%*s %255s", s1);
                S.RegisterMusicSet(s1);
            } else if (!strcmp(a, "musicmode")) {
                sscanf(txt, "%*s %d", &i1);
                cl[0x9AA] = (unsigned char)i1;
            } else if (!strcmp(a, "ambient")) {
                sscanf(txt, "%*s %255s", s1);
                S.BeginRegistration(); /* engine order: inside the level registration (SPEC-004) */
                /* a level registers the ambient waves it uses through sound configstrings; register all of them */
                for (i = 0; i < npents; i++)
                    if (!strncmp(pents[i].name, "sound/ambient/", 14))
                        S.RegisterSound(pents[i].name + 6);
                S.RegisterAmbientSet(s1);
                S.EndRegistration();
            } else if (!strcmp(a, "general")) {
                sscanf(txt, "%*s %d", &i1);
                *(unsigned short *)(cl + 0x9A8) = (unsigned short)i1;
            } else if (!strcmp(a, "precache")) {
                sscanf(txt, "%*s %d %255s", &i1, s1);
                ((void **)(cl + 0x3A004))[i1] = S.RegisterSound(s1);
            } else if (!strcmp(a, "loop")) {
                sscanf(txt, "%*s %d %d %d %i %f %f %f", &i1, &i2, &i3, &i4, &x, &y, &z);
                loops[i1].used = i3 != 0;
                loops[i1].ent = i2; loops[i1].sound = i3; loops[i1].parm = i4;
                loops[i1].org[0] = x; loops[i1].org[1] = y; loops[i1].org[2] = z;
                entorg[i2][0] = x; entorg[i2][1] = y; entorg[i2][2] = z;
            } else if (!strcmp(a, "cvar")) {
                sscanf(txt, "%*s %255s %255s", s1, s2);
                set_cvar(s1, s2);
            } else if (!strcmp(a, "playernum")) {
                sscanf(txt, "%*s %d", &i1);
                *(int *)(cl + 0x1F3C) = i1;
            } else if (!strcmp(a, "proctype")) {
                sscanf(txt, "%*s %d", &i1);
                S.SetSoundProcType((unsigned char)i1);
            } else if (!strcmp(a, "stopall")) {
                S.StopAllSounds();
            } else if (!strcmp(a, "activate")) {
                sscanf(txt, "%*s %d", &i1);
                S.Activate(i1);
            } else if (!strcmp(a, "paused")) {
                sscanf(txt, "%*s %255s", s1);
                set_cvar("paused", s1);
            } else if (!strcmp(a, "listener")) {
                sscanf(txt, "%*s %f %f %f %f", &lorg[0], &lorg[1], &lorg[2], &yaw);
            } else if (!strcmp(a, "cmd")) {
                char *p;
                strncpy(s1, txt + 4, 255);
                argc_ = 0;
                for (p = strtok(s1, " "); p && argc_ < 16; p = strtok(NULL, " "))
                    strcpy(argv_[argc_++], p);
                for (i = 0; i < MAXCMD; i++)
                    if (cmds[i].fn && argc_ && !_stricmp(cmds[i].name, argv_[0]))
                        cmds[i].fn();
            } else if (!strcmp(a, "quit")) {
                goto done;
            }
            next++;
        }
        yr = yaw * 3.14159265f / 180.0f;
        fwd[0] = cosf(yr); fwd[1] = sinf(yr); fwd[2] = 0;
        right[0] = sinf(yr); right[1] = -cosf(yr); right[2] = 0;
        *(int *)(cl + 0x1A3C) = (int)t;
        build_frame();
        S.Update(lorg, fwd, right, up);
        Sleep(4); /* ~200 fps frame loop, close to an uncapped game */
    }
done:
    S.Shutdown();
    logline("done %.3f\n", now_ms() - start);
    return 0;
}
