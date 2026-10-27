/* SPDX-License-Identifier: GPL-2.0-or-later
 * OpenMerc player module - black-box tracing proxy (PLAN.md "verification at module boundaries").
 *
 * Installed as base\player.dll, it loads the real module (env OMTRACE_TARGET, default player.dll.orig in the
 * same directory: the original or OpenMerc's), hands it wrapped copies of the three import tables and wraps the
 * inventory method table. Every crossing of the boundary is logged with its raw arguments and its result:
 *  - imports (common, client, server) through naked thunks that do not assume a signature: they log the first
 *    stack words, call the real function with the caller's stack untouched (cdecl) and log EAX/EDX (or ST0);
 *  - inventory methods (thiscall) the same way, through a copy of the method table;
 *  - after every inventory think: the 640-byte state block words that changed (SPEC-601 Interface 3, pointer
 *    words shown symbolically), the owner's player-state words that changed, the owner's weapon kick and
 *    looping sound, and the in-view instance (id, playing sequence path, shown parts, skin overrides).
 * Return addresses are kept on a private stack, so nested and re-entrant calls are fine.
 * Written from the interface specs only; nothing of the module's code is read.
 *
 * Environment: OMTRACE_TARGET (module file name), OMTRACE_LOG (log path; "{pid}" is replaced by the process id;
 * default omptrace.log next to the DLL), OMTRACE_VERBOSE=1 (also log the frequent getters and message I/O).
 * Implements: SPEC-003 (tables, inventory slots, export table), SPEC-600 (weapon descriptions),
 *             SPEC-601 (state block), SPEC-010/011 (read-only instance queries), SPEC-760 (entity prefix)
 */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "../../src/om_ghoul.h"
#include "../../src/om_player_api.h"

using namespace om;

static FILE *g_log;
static HMODULE g_target;
static int g_verbose;
static PlayerExport *g_orig; /* the real module's export table */
static PlayerExport g_exp;   /* the table handed to the engine/game */
static void *(*g_origNew)(void);
static float *g_svTime, *g_clTime;

static int ctxflag() { return (g_orig && g_orig->contextFlag) ? *g_orig->contextFlag : 0; }
static float now_any()
{
    float *t = ctxflag() ? g_clTime : g_svTime;
    return t ? *t : -1.f;
}

static void L(const char *fmt, ...)
{
    if (!g_log)
        return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(g_log, "%s%9.3f ", ctxflag() ? "C" : "S", now_any());
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static void self_dir(char *path, size_t n)
{
    HMODULE self;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&self_dir, &self);
    GetModuleFileNameA(self, path, (DWORD)n);
    char *s = strrchr(path, '\\');
    if (s)
        s[1] = 0;
}

static void open_log()
{
    if (g_log)
        return;
    char path[MAX_PATH], tmpl[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("OMTRACE_LOG", tmpl, sizeof(tmpl));
    if (!n || n >= sizeof(tmpl)) {
        self_dir(tmpl, sizeof(tmpl));
        strcat(tmpl, "omptrace.log");
    }
    char *p = strstr(tmpl, "{pid}");
    if (p) {
        *p = 0;
        _snprintf(path, sizeof(path), "%s%lu%s", tmpl, GetCurrentProcessId(), p + 5);
    } else
        strcpy(path, tmpl);
    g_log = fopen(path, "a");
    char v[8];
    g_verbose = GetEnvironmentVariableA("OMTRACE_VERBOSE", v, sizeof(v)) && v[0] == '1';
}

static bool load_target()
{
    if (g_target)
        return true;
    open_log();
    char path[MAX_PATH], name[MAX_PATH];
    self_dir(path, sizeof(path));
    if (!GetEnvironmentVariableA("OMTRACE_TARGET", name, sizeof(name)))
        strcpy(name, "player.dll.orig");
    strcat(path, name);
    g_target = LoadLibraryA(path);
    if (g_log)
        fprintf(g_log, "=== omptrace pid %lu: target %s -> %p\n", GetCurrentProcessId(), path, (void *)g_target);
    return g_target != NULL;
}

/* ===================================================================================================
 * Import thunks. Ids: common 0..18, client 32..51, server 64..101.
 * Argument format letters: i int, x hex, s string, f float, b bool (low byte), S item slot, c fourcc,
 * D data (pointer, length = next argument), ? one extra raw word (to discover unspecified arguments).
 * Result letters after '>': v void, i int, x hex, b bool (AL), s string, f float (ST0).
 * =================================================================================================== */
enum { IMP_COM = 0, IMP_CL = 32, IMP_SV = 64, IMP_N = 104 };
struct ImpInfo {
    const char *name;
    const char *fmt;
    bool quiet; /* logged only with OMTRACE_VERBOSE */
};
static void *g_impReal[IMP_N];
static ImpInfo g_imp[IMP_N];

static void set_imp(int id, const char *name, const char *fmt, bool quiet = false)
{
    g_imp[id].name = name;
    g_imp[id].fmt = fmt;
    g_imp[id].quiet = quiet;
}

static void init_imp_info()
{
    static bool done;
    if (done)
        return;
    done = true;
    static const char *comNames[19] = {"Cvar_Get", "Cvar_Set", "Cvar_SetValue", "Cmd_Argc", "Cmd_Argv",
                                       "Cmd_Args", "Com_Printf", "Com_sprintf", "Com_DPrintf", "FS_LoadFile",
                                       "FS_FreeFile", "irand", "ghl_specular", "NewPlayerModelInfo",
                                       "LoadGhoulFile", "ReadLine", "PrecacheGsq", "RegisterGsq", "ApplyGsqSkins"};
    for (int i = 0; i < 19; i++)
        set_imp(IMP_COM + i, comNames[i], "xxxx>x");
    set_imp(IMP_COM + 0, "Cvar_Get", "ssx>x");
    set_imp(IMP_COM + 3, "Cmd_Argc", ">i");
    set_imp(IMP_COM + 4, "Cmd_Argv", "i>s");
    set_imp(IMP_COM + 5, "Cmd_Args", ">s");
    set_imp(IMP_COM + 6, "Com_Printf", "s>v");
    set_imp(IMP_COM + 7, "Com_sprintf", "xis>v");
    set_imp(IMP_COM + 8, "Com_DPrintf", "s>v");
    set_imp(IMP_COM + 11, "irand", "ii>i");
    set_imp(IMP_COM + 14, "LoadGhoulFile", "ssx>i");
    set_imp(IMP_COM + 16, "PrecacheGsq", "ss>v");
    set_imp(IMP_COM + 17, "RegisterGsq", "ssx>b");
    set_imp(IMP_COM + 18, "ApplyGsqSkins", "ssxx>x");

    set_imp(IMP_CL + 0, "cl.GetGhoul", ">x", true);
    set_imp(IMP_CL + 2, "cl.FirePrimaryFx", "xxx?>v");
    set_imp(IMP_CL + 3, "cl.FireAltFx", "xxx?>v");
    set_imp(IMP_CL + 4, "cl.NoteSound", "s???>v");
    set_imp(IMP_CL + 5, "cl.KillSound", "??>v");
    set_imp(IMP_CL + 6, "cl.NoteEffect", "s???>v");
    set_imp(IMP_CL + 7, "cl.ReadByte", ">i", true);
    set_imp(IMP_CL + 8, "cl.ReadShort", ">i", true);
    set_imp(IMP_CL + 9, "cl.ReadLong", ">i", true);
    set_imp(IMP_CL + 10, "cl.SZReadByte", "x>i", true);
    set_imp(IMP_CL + 11, "cl.SZReadShort", "x>i", true);
    set_imp(IMP_CL + 12, "cl.SZReadLong", "x>i", true);
    set_imp(IMP_CL + 13, "cl.SZReadData", "xxi>v", true);
    set_imp(IMP_CL + 14, "cl.WriteByte", "xi>v", true);
    set_imp(IMP_CL + 15, "cl.WriteShort", "xi>v", true);
    set_imp(IMP_CL + 16, "cl.WriteLong", "xi>v", true);
    set_imp(IMP_CL + 17, "cl.SZ_Init", "xxi>v", true);
    set_imp(IMP_CL + 18, "cl.SZ_Clear", "x>v", true);
    set_imp(IMP_CL + 19, "cl.SZ_Write", "xDi>v", true);

    set_imp(IMP_SV + 0, "sv.GetGhoul", ">x", true);
    set_imp(IMP_SV + 2, "sv.FirePrimary", "xxx?>v");
    set_imp(IMP_SV + 3, "sv.FireAlt", "xxx?>v");
    set_imp(IMP_SV + 4, "sv.ReloadStarted", "x?>v");
    set_imp(IMP_SV + 5, "sv.DropWeapon", "xii?>v");
    set_imp(IMP_SV + 6, "sv.ItemUsed", "xi?>v");
    set_imp(IMP_SV + 7, "sv.DropItem", "xii?>v");
    set_imp(IMP_SV + 8, "sv.PrecacheAttack", "i?>v");
    set_imp(IMP_SV + 9, "sv.EffectIndex", "s>i");
    set_imp(IMP_SV + 10, "sv.SoundIndex", "s>i");
    set_imp(IMP_SV + 11, "sv.StartSound", "xxxxxx>v");
    set_imp(IMP_SV + 12, "sv.FlagTest", "x?>b");
    /* fire-time imports: reads are logged when they return a time (imp_post), clears always */
    set_imp(IMP_SV + 13, "sv.GetFireTime1", "x?>f", false);
    set_imp(IMP_SV + 14, "sv.ClearFireTime1", "x?>v", false);
    set_imp(IMP_SV + 15, "sv.GetFireTime2", "x?>f", false);
    set_imp(IMP_SV + 16, "sv.ClearFireTime2", "x?>v", false);
    set_imp(IMP_SV + 17, "sv.Cbuf_AddText", "s>v");
    set_imp(IMP_SV + 18, "sv.Cvar_BitInfo", "x>s");
    set_imp(IMP_SV + 19, "sv.PrintId", "xxxxx>v");
    set_imp(IMP_SV + 20, "sv.TextOfId", "x?>s");
    set_imp(IMP_SV + 21, "sv.UseFlashpack", "xS?>b");
    set_imp(IMP_SV + 22, "sv.Unused22", "xS?>b");
    set_imp(IMP_SV + 23, "sv.UseC4", "xS?>b");
    set_imp(IMP_SV + 24, "sv.UseClaymore", "xS?>b");
    set_imp(IMP_SV + 25, "sv.UseMedkit", "xS?>b");
    set_imp(IMP_SV + 26, "sv.UseGrenade", "xS?>b");
    set_imp(IMP_SV + 27, "sv.Goggles", "xS?>b");
    set_imp(IMP_SV + 28, "sv.WriteByte", "xi>v", true);
    set_imp(IMP_SV + 29, "sv.WriteShort", "xi>v", true);
    set_imp(IMP_SV + 30, "sv.WriteLong", "xi>v", true);
    set_imp(IMP_SV + 31, "sv.ReliableByte", "xi>v");
    set_imp(IMP_SV + 32, "sv.ReliableData", "Di>v");
    set_imp(IMP_SV + 33, "sv.SZ_Init", "xxi>v", true);
    set_imp(IMP_SV + 34, "sv.SZ_Clear", "x>v", true);
    set_imp(IMP_SV + 35, "sv.SZ_Write", "xDi>v", true);
    set_imp(IMP_SV + 36, "sv.WriteChunk", "cDi>v");
    set_imp(IMP_SV + 37, "sv.ReadChunk", "cxix>v");
}

static int fmt_args(char *out, int cap, const char *fmt, const unsigned *a)
{
    int n = 0;
    for (int i = 0; fmt[i] && fmt[i] != '>' && n < cap - 80; i++) {
        unsigned v = a[i];
        switch (fmt[i]) {
        case 'i': n += _snprintf(out + n, cap - n, " %d", (int)v); break;
        case 'b': n += _snprintf(out + n, cap - n, " %d", (int)(v & 0xFF)); break;
        case 'f': n += _snprintf(out + n, cap - n, " %.4f", *(const float *)&a[i]); break;
        case 'c': n += _snprintf(out + n, cap - n, " '%.4s'", (const char *)&a[i]); break;
        case 's':
            if (v && !IsBadStringPtrA((const char *)v, 256))
                n += _snprintf(out + n, cap - n, " \"%.200s\"", (const char *)v);
            else
                n += _snprintf(out + n, cap - n, " (str %x)", v);
            break;
        case 'S':
            if (v && !IsBadReadPtr((void *)v, 16)) {
                const ItemSlot *s = (const ItemSlot *)v;
                n += _snprintf(out + n, cap - n, " slot{%d,%d,%d,%.3f}", s->type, s->count, s->state, s->nextTime);
            } else
                n += _snprintf(out + n, cap - n, " slot(%x)", v);
            break;
        case 'D': {
            int len = (int)a[i + 1];
            n += _snprintf(out + n, cap - n, " [");
            for (int k = 0; k < len && k < 48 && v; k++)
                n += _snprintf(out + n, cap - n, "%02x", ((const unsigned char *)v)[k]);
            n += _snprintf(out + n, cap - n, len > 48 ? "..]" : "]");
            break;
        }
        case '?': n += _snprintf(out + n, cap - n, " ?%x", v); break;
        default: n += _snprintf(out + n, cap - n, " %x", v); break;
        }
    }
    return n;
}

static char result_kind(const char *fmt)
{
    const char *r = strchr(fmt, '>');
    return r ? r[1] : 'x';
}

static const char *caller_tag();

extern "C" void __cdecl imp_pre(int id, const unsigned *args)
{
    const ImpInfo &in = g_imp[id];
    if (!in.name || (in.quiet && !g_verbose))
        return;
    if ((id == IMP_SV + 13 || id == IMP_SV + 15) && !g_verbose)
        return; /* logged by imp_post when non-zero */
    char buf[1024];
    int n = fmt_args(buf, sizeof(buf), in.fmt, args);
    buf[n] = 0;
    if (id == IMP_SV + 19) {
        /* localized message: the arguments depend on the string id (ITEMS 0x30D: name; 0x30E: count, name) */
        const char *name = NULL;
        if (args[1] == 0x30E)
            name = (const char *)args[3];
        else if (args[1] == 0x30D)
            name = (const char *)args[2];
        if (args[1] == 0x30E)
            n = _snprintf(buf, sizeof(buf), " %x %x %d", args[0], args[1], (int)args[2]);
        else
            n = _snprintf(buf, sizeof(buf), " %x %x", args[0], args[1]);
        if (name && !IsBadStringPtrA(name, 64))
            _snprintf(buf + n, sizeof(buf) - n, " \"%s\"", name);
        else if (!name)
            _snprintf(buf + n, sizeof(buf) - n, " %x %x", args[2], args[3]);
    }
    if (id == IMP_SV + 2 || id == IMP_SV + 3 || id == IMP_CL + 2 || id == IMP_CL + 3) {
        /* fire callbacks: show the weapon id of the inventory argument (object offset 0x2C, SPEC-782) */
        const char *inv = (const char *)args[2];
        if (inv && !IsBadReadPtr(inv, 0x30))
            _snprintf(buf + n, sizeof(buf) - n, "  (w=%d)", *(const int *)(inv + 0x2C));
    }
    L("%s%s%s", in.name, buf, caller_tag());
}

static float g_fret;
extern "C" void __cdecl imp_post(int id, unsigned eaxv, unsigned edxv)
{
    const ImpInfo &in = g_imp[id];
    if (!in.name || (in.quiet && !g_verbose))
        return;
    char k = result_kind(in.fmt);
    switch (k) {
    case 'v': return;
    case 'i': L("  -> %d", (int)eaxv); break;
    case 'b': L("  -> %d", (int)(eaxv & 0xFF)); break;
    case 'f':
        if ((id == IMP_SV + 13 || id == IMP_SV + 15) && !g_verbose) {
            if (g_fret != 0.f)
                L("%s -> %.4f%s", in.name, g_fret, caller_tag());
        } else
            L("  -> %.4f", g_fret);
        break;
    case 's':
        if (eaxv && !IsBadStringPtrA((const char *)eaxv, 512))
            L("  -> \"%.400s\"", (const char *)eaxv);
        else
            L("  -> (str %x)", eaxv);
        break;
    default: L("  -> %x", eaxv); break;
    }
    (void)edxv;
}

/* private return stack */
enum { SSTACK = 4096 };
extern "C" {
unsigned g_sRet[SSTACK], g_sId[SSTACK], g_sThis[SSTACK];
unsigned g_sp;
unsigned g_tmpThis;
}

extern "C" __declspec(naked) void imp_common()
{
    __asm {
        pop eax                      /* id */
        pop edx                      /* caller's return address */
        mov ecx, g_sp
        mov g_sRet[ecx*4], edx
        mov g_sId[ecx*4], eax
        inc ecx
        mov g_sp, ecx
        mov edx, esp                 /* first argument */
        push edx
        push eax
        call imp_pre
        add esp, 8
        mov ecx, g_sp
        mov eax, g_sId[ecx*4-4]
        call dword ptr g_impReal[eax*4]
        push edx
        push eax
        mov ecx, g_sp
        push dword ptr g_sId[ecx*4-4]
        call imp_post
        add esp, 4
        pop eax
        pop edx
        mov ecx, g_sp
        dec ecx
        mov g_sp, ecx
        push dword ptr g_sRet[ecx*4]
        ret
    }
}

/* same for functions returning float in ST0 */
extern "C" __declspec(naked) void imp_common_f()
{
    __asm {
        pop eax
        pop edx
        mov ecx, g_sp
        mov g_sRet[ecx*4], edx
        mov g_sId[ecx*4], eax
        inc ecx
        mov g_sp, ecx
        mov edx, esp
        push edx
        push eax
        call imp_pre
        add esp, 8
        mov ecx, g_sp
        mov eax, g_sId[ecx*4-4]
        call dword ptr g_impReal[eax*4]
        fstp dword ptr g_fret
        push edx
        push eax
        mov ecx, g_sp
        push dword ptr g_sId[ecx*4-4]
        call imp_post
        add esp, 4
        pop eax
        pop edx
        fld dword ptr g_fret
        mov ecx, g_sp
        dec ecx
        mov g_sp, ecx
        push dword ptr g_sRet[ecx*4]
        ret
    }
}

#define ITHUNK(n) extern "C" __declspec(naked) void ithunk_##n() { __asm { push n } __asm { jmp imp_common } }
#define ITHUNKF(n) extern "C" __declspec(naked) void ithunk_##n() { __asm { push n } __asm { jmp imp_common_f } }
#define I10(a) ITHUNK(a##0) ITHUNK(a##1) ITHUNK(a##2) ITHUNK(a##3) ITHUNK(a##4) ITHUNK(a##5) ITHUNK(a##6) \
    ITHUNK(a##7) ITHUNK(a##8) ITHUNK(a##9)
/* common 0..18 */
ITHUNK(0) ITHUNK(1) ITHUNK(2) ITHUNK(3) ITHUNK(4) ITHUNK(5) ITHUNK(6) ITHUNK(7) ITHUNK(8) ITHUNK(9)
ITHUNK(10) ITHUNK(11) ITHUNK(12) ITHUNK(13) ITHUNK(14) ITHUNK(15) ITHUNK(16) ITHUNK(17) ITHUNK(18)
/* client 32..51 */
ITHUNK(32) ITHUNK(33) ITHUNK(34) ITHUNK(35) ITHUNK(36) ITHUNK(37) ITHUNK(38) ITHUNK(39) I10(4)
ITHUNK(50) ITHUNK(51)
/* server 64..101 (77 and 79 return float) */
ITHUNK(64) ITHUNK(65) ITHUNK(66) ITHUNK(67) ITHUNK(68) ITHUNK(69) ITHUNK(70) ITHUNK(71) ITHUNK(72) ITHUNK(73)
ITHUNK(74) ITHUNK(75) ITHUNK(76) ITHUNKF(77) ITHUNK(78) ITHUNKF(79)
I10(8) I10(9) ITHUNK(100) ITHUNK(101)

#define IP(n) (void *)ithunk_##n
static void *g_ithunk[IMP_N] = {
    IP(0), IP(1), IP(2), IP(3), IP(4), IP(5), IP(6), IP(7), IP(8), IP(9), IP(10), IP(11), IP(12), IP(13), IP(14),
    IP(15), IP(16), IP(17), IP(18), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    IP(32), IP(33), IP(34), IP(35), IP(36), IP(37), IP(38), IP(39), IP(40), IP(41), IP(42), IP(43), IP(44), IP(45),
    IP(46), IP(47), IP(48), IP(49), IP(50), IP(51), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    IP(64), IP(65), IP(66), IP(67), IP(68), IP(69), IP(70), IP(71), IP(72), IP(73), IP(74), IP(75), IP(76), IP(77),
    IP(78), IP(79), IP(80), IP(81), IP(82), IP(83), IP(84), IP(85), IP(86), IP(87), IP(88), IP(89), IP(90), IP(91),
    IP(92), IP(93), IP(94), IP(95), IP(96), IP(97), IP(98), IP(99), IP(100), IP(101), 0, 0,
};

/* Wrap a table of n pointers: data entries (listed in `data`) are passed through unchanged. */
static void wrap_table(void **dst, void *const *src, int n, int base, const int *data, int ndata)
{
    for (int i = 0; i < n; i++) {
        bool isData = false;
        for (int k = 0; k < ndata; k++)
            isData |= data[k] == i;
        g_impReal[base + i] = src[i];
        dst[i] = (isData || !src[i]) ? src[i] : g_ithunk[base + i];
    }
}

/* ===================================================================================================
 * Inventory method table (79 thiscall slots, SPEC-003)
 * =================================================================================================== */
enum { NSLOTS = 79 };
static void **g_realVt;
static void *g_wrapVt[NSLOTS];
extern "C" void *g_realVtCopy[NSLOTS];
void *g_realVtCopy[NSLOTS];
static unsigned char g_slotQuiet[NSLOTS];
static const char *g_slotFmt[NSLOTS];

static void init_slot_info()
{
    static bool done;
    if (done)
        return;
    done = true;
    static const int quiet[] = {21, 22, 23, 25, 26, 27, 28, 29, 30, 32, 33, 35, 37, 39, 41, 43, 45, 47,
                                49, 51, 53, 55, 60, 61, 68, 70, 71, 72, 73, 74};
    for (int q : quiet)
        g_slotQuiet[q] = 1;
    for (int i = 0; i < NSLOTS; i++)
        g_slotFmt[i] = "xxx>x";
    g_slotFmt[0] = "b>v";
    g_slotFmt[1] = "i>v";
    g_slotFmt[2] = ">v";
    g_slotFmt[3] = ">v";
    g_slotFmt[4] = "si>b";
    g_slotFmt[5] = ">v";
    g_slotFmt[6] = "i>b";
    g_slotFmt[7] = "i>v";
    g_slotFmt[8] = ">v";
    g_slotFmt[9] = "i>v";
    g_slotFmt[10] = "i>v";
    g_slotFmt[11] = ">v";
    g_slotFmt[12] = "ii>b";
    g_slotFmt[13] = "i>i";
    g_slotFmt[14] = "ii>b";
    g_slotFmt[15] = "ii>b";
    g_slotFmt[16] = "ii>b";
    g_slotFmt[17] = ">i";
    g_slotFmt[18] = "i>i";
    g_slotFmt[19] = "iii>b";
    g_slotFmt[20] = "i>i";
    g_slotFmt[21] = "i>b";
    g_slotFmt[22] = "i>i";
    g_slotFmt[23] = ">i";
    g_slotFmt[24] = ">i";
    g_slotFmt[25] = ">i";
    g_slotFmt[26] = ">i";
    g_slotFmt[27] = ">i";
    g_slotFmt[28] = ">i";
    g_slotFmt[29] = ">i";
    g_slotFmt[30] = "i>i";
    g_slotFmt[31] = "iff>i";
    g_slotFmt[32] = ">i";
    g_slotFmt[33] = ">i";
    g_slotFmt[34] = "x>v";
    g_slotFmt[35] = ">i";
    g_slotFmt[36] = ">v";
    for (int s = 37; s <= 56; s++)
        g_slotFmt[s] = (s & 1) ? ">i" : "i>v";
    g_slotFmt[49] = ">s";
    g_slotFmt[50] = "s>v";
    g_slotFmt[57] = ">b";
    g_slotFmt[58] = ">v";
    g_slotFmt[59] = ">v";
    g_slotFmt[60] = ">i";
    g_slotFmt[61] = "x>v";
    g_slotFmt[62] = ">v";
    g_slotFmt[63] = ">v";
    g_slotFmt[64] = "xx>i";
    g_slotFmt[65] = "bi>v";
    g_slotFmt[66] = "xs>b";
    g_slotFmt[67] = "xbbbb>v";
    for (int s = 68; s <= 72; s++)
        g_slotFmt[s] = ">v";
    g_slotFmt[73] = "i>v";
    g_slotFmt[74] = ">i";
    g_slotFmt[75] = ">v";
    g_slotFmt[76] = ">v";
    g_slotFmt[77] = "x>v";
    g_slotFmt[78] = "i>v";
}

static void dump_after_think(void *inv, bool afterThink = true);
static void dump_inventory_full(void *inv, const char *why);
static OwnerContext *g_lastCtx;

/* active inventory methods (innermost last): nested calls are marked with their caller's slot */
static int g_invStack[256], g_invDepth;
static const char *caller_tag()
{
    static char tag[32];
    if (g_invDepth <= 0)
        return "";
    _snprintf(tag, sizeof(tag), " <%02d", g_invStack[g_invDepth - 1]);
    return tag;
}

extern "C" void __cdecl inv_pre(int slot, void *self, const unsigned *args)
{
    const char *parent = g_invDepth > 0 ? caller_tag() : "";
    char parentCopy[32];
    strcpy(parentCopy, parent);
    if (g_invDepth < 256)
        g_invStack[g_invDepth] = slot;
    g_invDepth++;
    if (slot == 61) {
        g_lastCtx = (OwnerContext *)args[0];
    }
    if (slot == 3) {
        /* button and state changes of the owner context as think sees them, per inventory (the game fills the
           context after setting it, so it is read here and not at slot 61) */
        static struct {
            void *inv;
            int v[9];
        } last[16];
        const OwnerContext *c = *(OwnerContext *const *)((const char *)self + 0x1C + 0x190);
        if (c && !IsBadReadPtr(c, sizeof(OwnerContext))) {
            int v[9] = {c->attack, c->altAttack, c->extra1, c->extra2, c->leanPositive, c->leanNegative,
                        c->cinematicFreeze, c->remoteView, c->reloadOneShot};
            int k = 0;
            while (k < 15 && last[k].inv && last[k].inv != self)
                k++;
            if (last[k].inv != self || memcmp(last[k].v, v, sizeof(v))) {
                L("BTN %p att=%d alt=%d x1=%d x2=%d lean=%d/%d cin=%d rv=%d one=%d", self, v[0], v[1], v[2], v[3],
                  v[4], v[5], v[6], v[7], v[8]);
                last[k].inv = self;
                memcpy(last[k].v, v, sizeof(v));
            }
        }
    }
    /* frequent getters are logged only when nested in another inventory method (internal structure) */
    if (g_slotQuiet[slot] && !g_verbose && g_invDepth < 2)
        return;
    char buf[1024];
    int n = fmt_args(buf, sizeof(buf), g_slotFmt[slot], args);
    buf[n] = 0;
    if (slot == 61 && args[0] && g_verbose) {
        const OwnerContext *c = (const OwnerContext *)args[0];
        L("INV61 %p ctx owner=%p att=%d alt=%d x1=%d x2=%d lean=%d/%d cin=%d rv=%d kick=%p ft=%.3f one=%d", self,
          (void *)c->owner, c->attack, c->altAttack, c->extra1, c->extra2, c->leanPositive, c->leanNegative,
          c->cinematicFreeze, c->remoteView, (void *)c->kick, c->frameTen, c->reloadOneShot);
        return;
    }
    L("INV%02d %p%s%s", slot, self, buf, parentCopy);
}

extern "C" void __cdecl inv_post(int slot, void *self, unsigned eaxv, unsigned edxv)
{
    (void)edxv;
    if (g_invDepth > 0)
        g_invDepth--;
    if (slot == 3) {
        dump_after_think(self);
        return;
    }
    if (slot == 76 || slot == 77)
        dump_inventory_full(self, slot == 76 ? "after INVN read" : "after full state");
    if (g_slotQuiet[slot] && !g_verbose && g_invDepth < 1)
        return;
    char k = result_kind(g_slotFmt[slot]);
    if (k == 'v')
        return;
    if (k == 'b' && (eaxv >> 8))
        L("  INV%02d -> %d (eax %x)", slot, (int)(eaxv & 0xFF), eaxv);
    else if (k == 'b')
        L("  INV%02d -> %d", slot, (int)(eaxv & 0xFF));
    else if (k == 's')
        L("  INV%02d -> \"%.32s\"", slot, eaxv && !IsBadStringPtrA((const char *)eaxv, 32) ? (const char *)eaxv : "?");
    else
        L("  INV%02d -> %d", slot, (int)eaxv);
}

extern "C" __declspec(naked) void inv_common()
{
    __asm {
        pop eax                      /* slot */
        pop edx                      /* caller's return address */
        mov g_tmpThis, ecx
        mov ecx, g_sp
        mov g_sRet[ecx*4], edx
        mov g_sId[ecx*4], eax
        mov edx, g_tmpThis
        mov g_sThis[ecx*4], edx
        inc ecx
        mov g_sp, ecx
        mov edx, esp
        push edx
        push dword ptr g_tmpThis
        push eax
        call inv_pre
        add esp, 12
        mov edx, g_sp
        mov ecx, g_sThis[edx*4-4]
        mov eax, g_sId[edx*4-4]
        call dword ptr g_realVtCopy[eax*4]   /* thiscall: the method removes its own arguments */
        push edx
        push eax
        mov ecx, g_sp
        push dword ptr g_sThis[ecx*4-4]
        push dword ptr g_sId[ecx*4-4]
        call inv_post
        add esp, 8
        pop eax
        pop edx
        mov ecx, g_sp
        dec ecx
        mov g_sp, ecx
        push dword ptr g_sRet[ecx*4]
        mov ecx, g_sThis[ecx*4]
        ret
    }
}

#define VTHUNK(n) extern "C" __declspec(naked) void vthunk_##n() { __asm { push n } __asm { jmp inv_common } }
#define V10(a) VTHUNK(a##0) VTHUNK(a##1) VTHUNK(a##2) VTHUNK(a##3) VTHUNK(a##4) VTHUNK(a##5) VTHUNK(a##6) \
    VTHUNK(a##7) VTHUNK(a##8) VTHUNK(a##9)
VTHUNK(0) VTHUNK(1) VTHUNK(2) VTHUNK(3) VTHUNK(4) VTHUNK(5) VTHUNK(6) VTHUNK(7) VTHUNK(8) VTHUNK(9)
V10(1) V10(2) V10(3) V10(4) V10(5) V10(6)
VTHUNK(70) VTHUNK(71) VTHUNK(72) VTHUNK(73) VTHUNK(74) VTHUNK(75) VTHUNK(76) VTHUNK(77) VTHUNK(78)
#define TP(a) (void *)vthunk_##a
static void *g_vthunks[NSLOTS] = {
    TP(0), TP(1), TP(2), TP(3), TP(4), TP(5), TP(6), TP(7), TP(8), TP(9),
    TP(10), TP(11), TP(12), TP(13), TP(14), TP(15), TP(16), TP(17), TP(18), TP(19),
    TP(20), TP(21), TP(22), TP(23), TP(24), TP(25), TP(26), TP(27), TP(28), TP(29),
    TP(30), TP(31), TP(32), TP(33), TP(34), TP(35), TP(36), TP(37), TP(38), TP(39),
    TP(40), TP(41), TP(42), TP(43), TP(44), TP(45), TP(46), TP(47), TP(48), TP(49),
    TP(50), TP(51), TP(52), TP(53), TP(54), TP(55), TP(56), TP(57), TP(58), TP(59),
    TP(60), TP(61), TP(62), TP(63), TP(64), TP(65), TP(66), TP(67), TP(68), TP(69),
    TP(70), TP(71), TP(72), TP(73), TP(74), TP(75), TP(76), TP(77), TP(78),
};

/* ===================================================================================================
 * State snapshots
 * =================================================================================================== */
struct Snap {
    void *inv;
    unsigned blk[160];
    unsigned char ps[200];
    float kick[3];
    unsigned es[2];
    char seq[160];
    char parts[160];
    char skins[64];
    int instId, on;
    bool valid, psValid;
};
static Snap g_snaps[16];

static Snap *snap_for(void *inv)
{
    for (auto &s : g_snaps)
        if (s.inv == inv)
            return &s;
    for (auto &s : g_snaps)
        if (!s.inv) {
            memset(&s, 0, sizeof(s));
            s.inv = inv;
            return &s;
        }
    memset(&g_snaps[0], 0, sizeof(Snap));
    g_snaps[0].inv = inv;
    return &g_snaps[0];
}

/* symbolic value of a word: pointers known to the inventory are named */
static void word_text(char *out, size_t cap, unsigned v, void *inv, const unsigned *blk)
{
    if (v && v == blk[0x130 / 4])
        _snprintf(out, cap, "@inst");
    else if (v && v == blk[0x134 / 4])
        _snprintf(out, cap, "@next");
    else if (v && v == (unsigned)(size_t)inv)
        _snprintf(out, cap, "@inv");
    else
        _snprintf(out, cap, "%08x", v);
}

static void inst_text(IGhoulInst *inst, char *seq, size_t nseq, char *parts, size_t nparts, char *skins,
                      size_t nskins, int *id, int *on)
{
    seq[0] = parts[0] = skins[0] = 0;
    *id = 0;
    *on = 0;
    if (!inst)
        return;
    *id = inst->GetInstanceId();
    *on = inst->IsOn() ? 1 : 0;
    IGhoulObject *obj = inst->GetGhoulObject();
    GhoulID s = inst->GetPlayingSequence();
    char path[512] = "";
    if (obj && s)
        obj->GetSequencePath(s, path);
    const char *b = strrchr(path, '/');
    _snprintf(seq, nseq, "%d:%s", (int)s, b ? b + 1 : path);
    if (!obj)
        return;
    int np = obj->NumParts();
    size_t k = 0;
    for (int p = 1; p <= np && k + 1 < nparts; p++)
        parts[k++] = obj->PartType(p) == 1 ? (inst->IsPartOn(p) ? '1' : '0') : '.';
    parts[k] = 0;
    int nm = obj->NumMaterials();
    size_t m = 0;
    for (int i = 1; i <= nm && m + 8 < nskins; i++)
        m += _snprintf(skins + m, nskins - m, "%d,", (int)inst->GetSkin(i, 0, 0));
    /* the instance transform, when it is not the identity (who turns the model, SPEC-011 matrix) */
    float t[16];
    inst->GetTransform16(t);
    static const float ident16[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    bool ident = true;
    for (int i = 0; i < 16; i++)
        ident &= t[i] > ident16[i] - 0.0001f && t[i] < ident16[i] + 0.0001f;
    if (!ident)
        _snprintf(seq + strlen(seq), nseq - strlen(seq), " xf=[%.2f %.2f %.2f|%.2f %.2f %.2f|%.2f %.2f %.2f|%.1f %.1f %.1f]",
                  t[0], t[1], t[2], t[4], t[5], t[6], t[8], t[9], t[10], t[12], t[13], t[14]);
}

/* names of an in-view model object's parts, materials and skins, printed once per object */
static void describe_object(IGhoulObject *obj)
{
    static void *seen[64];
    static int nseen;
    for (int i = 0; i < nseen; i++)
        if (seen[i] == obj)
            return;
    if (nseen < 64)
        seen[nseen++] = obj;
    char line[4096], name[256];
    int n = _snprintf(line, sizeof(line), "OBJ %p id=%d parts:", (void *)obj, (int)obj->GetObjectId());
    for (int p = 1; p <= obj->NumParts() && n < 3800; p++) {
        name[0] = 0;
        obj->GetPartName(p, name);
        n += _snprintf(line + n, sizeof(line) - n, " %d:%s/%d", p, name, obj->PartType(p));
    }
    L("%s", line);
    n = _snprintf(line, sizeof(line), "OBJ %p materials:", (void *)obj);
    for (int m = 1; m <= obj->NumMaterials() && n < 3800; m++) {
        name[0] = 0;
        obj->GetMaterialName(m, name);
        n += _snprintf(line + n, sizeof(line) - n, " %d:%s", m, name);
    }
    n += _snprintf(line + n, sizeof(line) - n, " skins:");
    for (int k = 1; k <= obj->NumSkins() && n < 3800; k++) {
        name[0] = 0;
        obj->GetSkinName(k, name);
        n += _snprintf(line + n, sizeof(line) - n, " %d:%s(m%d)", k, name, (int)obj->SkinMaterial(k));
    }
    n += _snprintf(line + n, sizeof(line) - n, " notes:");
    for (int k = 1; k <= obj->NumNotes() && n < 3800; k++)
        n += _snprintf(line + n, sizeof(line) - n, " %d", k);
    n += _snprintf(line + n, sizeof(line) - n, " sequences: %d", (int)obj->NumSequences());
    L("%s", line);
}

static void dump_after_think(void *inv, bool afterThink)
{
    const unsigned *blk = (const unsigned *)((const char *)inv + 0x1C);
    Snap *s = snap_for(inv);
    char line[4096];
    int n = 0;
    for (int i = 0; i < 160; i++) {
        if (s->valid && blk[i] == s->blk[i])
            continue;
        int off = i * 4;
        if (off >= 0x214 && off < 0x254)
            continue; /* sequence name, printed below */
        char w[16];
        word_text(w, sizeof(w), blk[i], inv, blk);
        if (off == 0x130 || off == 0x134 || off == 0x190)
            _snprintf(w, sizeof(w), "%s", blk[i] ? "ptr" : "0");
        n += _snprintf(line + n, sizeof(line) - n, " %03x:%s", off, w);
        if (n > 3900)
            break;
    }
    const char *seqName = (const char *)blk + 0x214;
    if (!s->valid || memcmp(seqName, (const char *)s->blk + 0x214, 64))
        n += _snprintf(line + n, sizeof(line) - n, " seq=\"%.64s\"", seqName);
    if (n)
        L("BLK %p%s", inv, line);
    memcpy(s->blk, blk, sizeof(s->blk));

    char seq[160], parts[160], skins[64];
    int id, on;
    IGhoulInst *inst = (IGhoulInst *)(size_t)blk[0x130 / 4];
    if (inst && inst->GetGhoulObject())
        describe_object(inst->GetGhoulObject());
    inst_text(inst, seq, sizeof(seq), parts, sizeof(parts), skins, sizeof(skins), &id, &on);
    if (!s->valid || id != s->instId || on != s->on || strcmp(seq, s->seq) || strcmp(parts, s->parts) ||
        strcmp(skins, s->skins)) {
        L("INST %p id=%d on=%d seq=%s parts=%s skins=%s", inv, id, on, seq, parts, skins);
        s->instId = id, s->on = on;
        strcpy(s->seq, seq), strcpy(s->parts, parts), strcpy(s->skins, skins);
    }

    /* the owner context lives in the caller's stack frame: only valid right after think */
    OwnerContext *ctx = afterThink ? (OwnerContext *)(size_t)blk[0x190 / 4] : NULL;
    if (ctx && IsBadReadPtr(ctx, sizeof(OwnerContext)))
        ctx = NULL;
    const unsigned char *ed = ctx ? (const unsigned char *)ctx->owner : NULL;
    const unsigned char *cl = ed && !IsBadReadPtr(ed, 0x120) ? *(const unsigned char *const *)(ed + EDICT_CLIENT) : NULL;
    if (cl && !IsBadReadPtr(cl, 200)) {
        n = 0;
        for (int i = 0; i < 200; i += 4) {
            if (s->psValid && !memcmp(cl + i, s->ps + i, 4))
                continue;
            char w[16];
            word_text(w, sizeof(w), *(const unsigned *)(cl + i), inv, blk);
            n += _snprintf(line + n, sizeof(line) - n, " %02x:%s", i, w);
        }
        if (n)
            L("PS%s", line);
        memcpy(s->ps, cl, 200);
        s->psValid = true;
    }
    if (ctx && ctx->kick && !IsBadReadPtr(ctx->kick, 12)) {
        if (memcmp(ctx->kick, s->kick, 12))
            L("KICK %.3f %.3f %.3f", ctx->kick[0], ctx->kick[1], ctx->kick[2]);
        memcpy(s->kick, ctx->kick, 12);
    }
    if (ed && !IsBadReadPtr(ed, 0x48)) {
        const unsigned *es = (const unsigned *)(ed + EDICT_LOOP_SOUND);
        if (memcmp(es, s->es, 8))
            L("ES sound %08x data %08x", es[0], es[1]);
        memcpy(s->es, es, 8);
    }
    s->valid = true;
}

static void dump_inventory_full(void *inv, const char *why)
{
    Snap *s = snap_for(inv);
    s->valid = false;
    L("FULL %p (%s)", inv, why);
    dump_after_think(inv, false);
}

/* weapon descriptions (SPEC-600 Interface 2): data members and the two virtual booleans */
typedef bool(__thiscall *BoolFn)(void *);
static void dump_weapon_descs()
{
    static bool done;
    if (done || !g_orig || !g_orig->weaponDescs)
        return;
    done = true;
    for (int w = 0; w < 15; w++) {
        const char *d = (const char *)g_orig->weaponDescs[w];
        if (!d) {
            L("DESC %d null", w);
            continue;
        }
        void **vt = *(void ***)d;
        bool muzzle = ((BoolFn)vt[0x70 / 4])((void *)d);
        bool conv = ((BoolFn)vt[0x74 / 4])((void *)d);
        L("DESC %2d clip=%d wt=%d ammo=%d/%d v14=%g/%g spread=%g/%g max=%g/%g dir=%s ac=%d kick=%g/%g "
          "att=%d/%d c0=%d model=%x muzzle=%d conv=%d",
          w, *(int *)(d + 4), *(int *)(d + 8), *(int *)(d + 0xC), *(int *)(d + 0x10), *(float *)(d + 0x14),
          *(float *)(d + 0x18), *(float *)(d + 0x1C), *(float *)(d + 0x20), *(float *)(d + 0x24),
          *(float *)(d + 0x28), d + 0x2C, *(short *)(d + 0xAC), *(float *)(d + 0xB0), *(float *)(d + 0xB4),
          *(int *)(d + 0xB8), *(int *)(d + 0xBC), *(int *)(d + 0xC0), *(unsigned *)(d + 0xC4), (int)muzzle,
          (int)conv);
    }
}

/* ===================================================================================================
 * Exports
 * =================================================================================================== */
static void *my_new()
{
    void *inv = g_origNew();
    if (!inv)
        return inv;
    void **vt = *(void ***)inv;
    if (!g_realVt) {
        g_realVt = vt;
        memcpy(g_realVtCopy, vt, sizeof(g_realVtCopy));
        memcpy(g_wrapVt, g_vthunks, sizeof(g_wrapVt));
    }
    if (vt == g_realVt)
        *(void ***)inv = g_wrapVt;
    L("NEWINV %p (vt %s)", inv, vt == g_realVt ? "wrapped" : "OTHER, not wrapped");
    Snap *s = snap_for(inv);
    s->valid = false;
    dump_after_think(inv, false);
    return inv;
}
static void my_delete(void *inv)
{
    L("DELINV %p", inv);
    for (auto &s : g_snaps)
        if (s.inv == inv)
            s.inv = NULL;
    if (inv && *(void ***)inv == g_wrapVt)
        *(void ***)inv = g_realVt;
    g_orig->DeleteInventory(inv);
}
static void my_precache(unsigned mask)
{
    L("PRECACHEVIEW %x", mask);
    g_orig->PrecacheViewModels(mask);
    L("PRECACHEVIEW done");
}
static void my_forget(void) { L("FORGETVIEW"); g_orig->ForgetViewModels(); }
static void my_rebuild(void *inv)
{
    L("REBUILD %p", inv);
    g_orig->RebuildAfterRestore(inv);
    dump_inventory_full(inv, "after rebuild");
}
static int my_init(void)
{
    int r = g_orig->Init();
    L("INIT -> %d", r);
    return r;
}
static void my_shutdown(void) { L("SHUTDOWN"); g_orig->Shutdown(); }

static void build_exports()
{
    g_exp = *g_orig;
    g_origNew = g_orig->NewInventory;
    g_exp.Init = my_init;
    g_exp.Shutdown = my_shutdown;
    g_exp.NewInventory = my_new;
    g_exp.DeleteInventory = my_delete;
    g_exp.PrecacheViewModels = my_precache;
    g_exp.ForgetViewModels = my_forget;
    g_exp.RebuildAfterRestore = my_rebuild;
}

typedef PlayerExport *(__cdecl *GetApi)(void *, void *);
static void *g_comWrap[19], *g_clWrap[20], *g_svWrap[38];

static void *wrap_common(void *common)
{
    static const int data[] = {12};
    wrap_table(g_comWrap, (void *const *)common, 19, IMP_COM, data, 1);
    return g_comWrap;
}

extern "C" __declspec(dllexport) PlayerExport *__cdecl GetPlayerClientAPI(void *common, void *client)
{
    if (!load_target())
        return NULL;
    init_imp_info();
    init_slot_info();
    GetApi f = (GetApi)GetProcAddress(g_target, "GetPlayerClientAPI");
    static const int data[] = {1};
    wrap_table(g_clWrap, (void *const *)client, 20, IMP_CL, data, 1);
    g_clTime = ((PlayerClientImport *)client)->time;
    PlayerExport *r = f(wrap_common(common), g_clWrap);
    if (g_log)
        fprintf(g_log, "=== GetPlayerClientAPI -> %p version %d\n", (void *)r, r ? r->version : -1);
    if (!r)
        return NULL;
    g_orig = r;
    build_exports();
    dump_weapon_descs();
    return &g_exp;
}

extern "C" __declspec(dllexport) PlayerExport *__cdecl GetPlayerServerAPI(void *common, void *server)
{
    if (!load_target())
        return NULL;
    init_imp_info();
    init_slot_info();
    GetApi f = (GetApi)GetProcAddress(g_target, "GetPlayerServerAPI");
    static const int data[] = {1};
    wrap_table(g_svWrap, (void *const *)server, 38, IMP_SV, data, 1);
    g_svTime = ((PlayerServerImport *)server)->time;
    PlayerExport *r = f(wrap_common(common), g_svWrap);
    if (g_log)
        fprintf(g_log, "=== GetPlayerServerAPI -> %p version %d\n", (void *)r, r ? r->version : -1);
    if (!r)
        return NULL;
    g_orig = r;
    build_exports();
    dump_weapon_descs();
    return &g_exp;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_DETACH) {
        if (g_log) {
            fprintf(g_log, "=== omptrace detach (%s)\n", reserved ? "process exit" : "FreeLibrary");
            fclose(g_log);
            g_log = NULL;
        }
        /* the engine released the module: release the target too, so that the next load starts it fresh */
        if (g_target && !reserved)
            FreeLibrary(g_target);
        g_target = NULL;
    }
    return TRUE;
}
