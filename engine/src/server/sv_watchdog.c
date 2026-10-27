/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc server: stall watchdog (a test aid, not in the original). With cvar
 * sv_watchdog <seconds> > 0, a background thread watches the heartbeat that
 * SV_Frame and the shutdown path give; when it has not moved for that long it
 * writes the call stacks of every thread of the process to
 * <user>/watchdog.txt (once per stall). Frames inside OpenMerc modules are
 * named from their symbols; frames inside any other module (the original game
 * and player modules, system DLLs) are given by module name only - no
 * addresses inside foreign modules are recorded (clean-room rule).
 *
 * Implements: SPEC-001 host diagnostics (no game behaviour)
 */

#include <stdio.h>

#include "header/server.h"

#ifdef _WIN32

#include <windows.h>
#include <tlhelp32.h>
#include <ctype.h>
#include <string.h>

/* DbgHelp, loaded at run time */
typedef struct
{
	DWORD SizeOfStruct;
	DWORD TypeIndex;
	ULONG64 Reserved[2];
	ULONG Index;
	ULONG Size;
	ULONG64 ModBase;
	ULONG Flags;
	ULONG64 Value;
	ULONG64 Address;
	ULONG Register;
	ULONG Scope;
	ULONG Tag;
	ULONG NameLen;
	ULONG MaxNameLen;
	CHAR Name[1];
} om_symbol_info_t;

typedef struct
{
	DWORD SizeOfStruct;
	PVOID Key;
	DWORD LineNumber;
	PCHAR FileName;
	DWORD64 Address;
} om_line64_t;

typedef enum { om_AddrMode1616, om_AddrMode1632, om_AddrModeReal, om_AddrModeFlat } om_addrmode_t;

typedef struct
{
	DWORD64 Offset;
	WORD Segment;
	om_addrmode_t Mode;
} om_address64_t;

typedef struct
{
	DWORD64 Thread;
	DWORD ThCallbackStack;
	DWORD ThCallbackBStore;
	DWORD NextCallback;
	DWORD FramePointer;
	DWORD64 KiCallUserMode;
	DWORD64 KeUserCallbackDispatcher;
	DWORD64 SystemRangeStart;
	DWORD64 KiUserExceptionDispatcher;
	DWORD64 StackBase;
	DWORD64 StackLimit;
	DWORD64 Reserved[5];
} om_kdhelp64_t;

typedef struct
{
	om_address64_t AddrPC;
	om_address64_t AddrReturn;
	om_address64_t AddrFrame;
	om_address64_t AddrStack;
	om_address64_t AddrBStore;
	PVOID FuncTableEntry;
	DWORD64 Params[4];
	BOOL Far;
	BOOL Virtual;
	DWORD64 Reserved[3];
	om_kdhelp64_t KdHelp;
} om_stackframe64_t;

typedef BOOL (WINAPI *SymInitialize_t)(HANDLE, PCSTR, BOOL);
typedef DWORD (WINAPI *SymSetOptions_t)(DWORD);
typedef BOOL (WINAPI *SymFromAddr_t)(HANDLE, DWORD64, PDWORD64, om_symbol_info_t *);
typedef BOOL (WINAPI *SymGetLineFromAddr64_t)(HANDLE, DWORD64, PDWORD, om_line64_t *);
typedef BOOL (WINAPI *StackWalk64_t)(DWORD, HANDLE, HANDLE, om_stackframe64_t *, PVOID, PVOID, PVOID, PVOID, PVOID);
typedef BOOL (WINAPI *SymRefreshModuleList_t)(HANDLE);

static SymInitialize_t pSymInitialize;
static SymSetOptions_t pSymSetOptions;
static SymFromAddr_t pSymFromAddr;
static SymGetLineFromAddr64_t pSymGetLineFromAddr64;
static StackWalk64_t pStackWalk64;
static SymRefreshModuleList_t pSymRefreshModuleList;
static PVOID pSymFunctionTableAccess64, pSymGetModuleBase64;

static cvar_t *sv_watchdog;
static volatile LONG wd_beat;
static volatile LONG wd_armed;
static volatile const char *wd_where = "";
static HANDLE wd_thread;
static char wd_path[MAX_OSPATH];

void
SV_Watchdog_Beat(const char *where)
{
	InterlockedIncrement(&wd_beat);
	wd_where = where;
	wd_armed = 1;
}

/* names the step the main thread is in, without counting as progress */
void
SV_Watchdog_Mark(const char *where)
{
	wd_where = where;
}

void
SV_Watchdog_Disarm(void)
{
	wd_armed = 0;
}

/* OpenMerc modules: symbols are printed; any other module only by name */
static qboolean
WD_OwnModule(const char *path)
{
	const char *base = strrchr(path, '\\');
	char b[MAX_OSPATH];
	int i;

	base = base ? base + 1 : path;
	Q_strlcpy(b, base, sizeof(b));

	for (i = 0; b[i]; i++)
	{
		b[i] = (char)tolower((unsigned char)b[i]);
	}

	/* the engine is copied into the test copies as SoF.exe: ours is the
	   module that holds this function */
	{
		HMODULE self = NULL;
		char selfpath[MAX_OSPATH];

		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					(LPCSTR)(void *)WD_OwnModule, &self) &&
				GetModuleFileNameA(self, selfpath, sizeof(selfpath)) && !Q_stricmp(selfpath, path))
		{
			return true;
		}
	}

	return !strncmp(b, "ref_gl3", 7) || !strncmp(b, "ref_omgl", 8) || !strncmp(b, "ref_gles3", 9) ||
		!strcmp(b, "omsnd.dll") || !strcmp(b, "openMerc.exe") || !strcmp(b, "openMerc-ded.exe");
}

static qboolean
WD_SystemModule(const char *path)
{
	char windir[MAX_OSPATH];
	UINT n = GetWindowsDirectoryA(windir, sizeof(windir));

	return (n > 0) && (n < sizeof(windir)) && !Q_strncasecmp(path, windir, (int)n);
}

/* one code address: OpenMerc and Windows modules with their symbol, any
   other module (the original game and player modules) by name only */
static void
WD_PrintAddr(FILE *f, HANDLE process, DWORD64 pc, const char *prefix)
{
	HMODULE mod = NULL;
	char modpath[MAX_OSPATH] = "?";
	const char *base;

	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				(LPCSTR)(UINT_PTR)pc, &mod))
	{
		GetModuleFileNameA(mod, modpath, sizeof(modpath));
	}

	base = strrchr(modpath, '\\');
	base = base ? base + 1 : modpath;

	if (mod && (WD_OwnModule(modpath) || WD_SystemModule(modpath)))
	{
		char buf[sizeof(om_symbol_info_t) + 256];
		om_symbol_info_t *sym = (om_symbol_info_t *)buf;
		DWORD64 disp = 0;
		om_line64_t line;
		DWORD ldisp = 0;

		memset(buf, 0, sizeof(buf));
		sym->SizeOfStruct = sizeof(om_symbol_info_t);
		sym->MaxNameLen = 255;
		memset(&line, 0, sizeof(line));
		line.SizeOfStruct = sizeof(line);

		fprintf(f, "  %s%s!%s", prefix ? prefix : "", base, pSymFromAddr(process, pc, &disp, sym) ? sym->Name : "?");

		if (WD_OwnModule(modpath) && pSymGetLineFromAddr64(process, pc, &ldisp, &line))
		{
			const char *fn = strrchr(line.FileName, '\\');

			fprintf(f, " (%s:%lu)", fn ? fn + 1 : line.FileName, (unsigned long)line.LineNumber);
		}

		fprintf(f, "\n");
	}
	else
	{
		/* the module offset only (no symbol): enough to look an address up in our own module's map or PDB when the
		   module under that name is OpenMerc's gamex86.dll / player.dll */
		if (mod)
		{
			fprintf(f, "  %s<%s+0x%lX>\n", prefix ? prefix : "", base, (unsigned long)(pc - (DWORD64)(UINT_PTR)mod));
		}
		else
		{
			fprintf(f, "  %s<%s>\n", prefix ? prefix : "", base);
		}
	}
}

/* values on the stack that point into executable image code */
static void
WD_ScanStack(FILE *f, HANDLE process, HANDLE th, DWORD64 sp)
{
	static UINT_PTR words[8192];
	SIZE_T got = 0;
	SIZE_T i, n;
	HMODULE lastforeign = NULL;
	int printed = 0;

	(void)th;

	/* read as much of the 32 KB above the stack pointer as is committed */
	for (n = sizeof(words); n >= 4096; n /= 2)
	{
		if (ReadProcessMemory(process, (LPCVOID)(UINT_PTR)sp, words, n, &got) && got)
		{
			break;
		}
	}

	fprintf(f, " stack scan (%lu bytes):\n", (unsigned long)got);

	for (i = 0; i < got / sizeof(UINT_PTR) && printed < 80; i++)
	{
		MEMORY_BASIC_INFORMATION mbi;
		HMODULE mod = NULL;
		char modpath[MAX_OSPATH];

		if (words[i] < 0x10000)
		{
			continue;
		}

		if (!VirtualQuery((LPCVOID)words[i], &mbi, sizeof(mbi)) || (mbi.Type != MEM_IMAGE) ||
				!(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
		{
			continue;
		}

		if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					(LPCSTR)words[i], &mod) || !GetModuleFileNameA(mod, modpath, sizeof(modpath)))
		{
			continue;
		}

		if (!WD_OwnModule(modpath) && !WD_SystemModule(modpath))
		{
			/* foreign module: one line per run */
			if (mod == lastforeign)
			{
				continue;
			}

			lastforeign = mod;
		}
		else
		{
			lastforeign = NULL;
		}

		WD_PrintAddr(f, process, words[i], "? ");
		printed++;
	}
}

#ifdef _M_IX86
#define WD_MACHINE IMAGE_FILE_MACHINE_I386
#define WD_SP(c) ((c)->Esp)
#else
#define WD_MACHINE IMAGE_FILE_MACHINE_AMD64
#define WD_SP(c) ((c)->Rsp)
#endif

/* walks a thread from a context (the context is changed); scan: also list the
   return-address candidates on the stack (frame-pointer-less code, such as the
   original modules, stops the walk early) */
static void
WD_DumpContext(FILE *f, HANDLE process, HANDLE th, CONTEXT *ctx, qboolean scan)
{
	om_stackframe64_t sf;
	DWORD64 sp = WD_SP(ctx);
	int depth;

	memset(&sf, 0, sizeof(sf));
#ifdef _M_IX86
	sf.AddrPC.Offset = ctx->Eip;
	sf.AddrFrame.Offset = ctx->Ebp;
	sf.AddrStack.Offset = ctx->Esp;
#else
	sf.AddrPC.Offset = ctx->Rip;
	sf.AddrFrame.Offset = ctx->Rbp;
	sf.AddrStack.Offset = ctx->Rsp;
#endif
	sf.AddrPC.Mode = om_AddrModeFlat;
	sf.AddrFrame.Mode = om_AddrModeFlat;
	sf.AddrStack.Mode = om_AddrModeFlat;

	for (depth = 0; depth < 48; depth++)
	{
		if (!pStackWalk64(WD_MACHINE, process, th, &sf, ctx, NULL, pSymFunctionTableAccess64,
					pSymGetModuleBase64, NULL) || !sf.AddrPC.Offset)
		{
			break;
		}

		WD_PrintAddr(f, process, sf.AddrPC.Offset, NULL);
	}

	if (scan)
	{
		WD_ScanStack(f, process, th, sp);
	}
}

static void
WD_DumpThread(FILE *f, HANDLE process, DWORD tid, qboolean ismain)
{
	HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
	CONTEXT ctx;

	if (!th)
	{
		return;
	}

	fprintf(f, "thread %lu%s\n", (unsigned long)tid, ismain ? " (main)" : "");

	if (SuspendThread(th) == (DWORD)-1)
	{
		CloseHandle(th);
		return;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.ContextFlags = CONTEXT_FULL;

	if (GetThreadContext(th, &ctx))
	{
		WD_DumpContext(f, process, th, &ctx, ismain);
	}

	ResumeThread(th);
	CloseHandle(th);
}

/* unhandled exception: report where (OpenMerc frames with symbols, foreign
   modules by name) and end the process instead of the system's error dialog,
   which a test desktop never shows */
static LONG WINAPI
WD_Crash(EXCEPTION_POINTERS *ep)
{
	FILE *f = fopen(wd_path, "a");

	if (f)
	{
		CONTEXT ctx = *ep->ContextRecord;
		const EXCEPTION_RECORD *er = ep->ExceptionRecord;

		fprintf(f, "=== crash: exception %08lx, last at \"%s\"\n", (unsigned long)er->ExceptionCode,
				(const char *)wd_where);

		if ((er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) && (er->NumberParameters >= 2))
		{
			fprintf(f, " %s of address %p\n", er->ExceptionInformation[0] == 0 ? "read" :
					(er->ExceptionInformation[0] == 1 ? "write" : "execute"), (void *)er->ExceptionInformation[1]);
		}

		WD_PrintAddr(f, GetCurrentProcess(), (DWORD64)(UINT_PTR)er->ExceptionAddress, "at ");
		pSymRefreshModuleList(GetCurrentProcess());
		WD_DumpContext(f, GetCurrentProcess(), GetCurrentThread(), &ctx, true);
		SV_ImportTrace_Dump(f);
		SV_World_DumpLastFilter(f);
		fprintf(f, "=== end\n");
		fclose(f);
	}

	Com_Printf("OMCRASH: unhandled exception %08lx, see %s\n", (unsigned long)ep->ExceptionRecord->ExceptionCode,
			wd_path);
	return EXCEPTION_EXECUTE_HANDLER;
}

static void
WD_CrashReport(void *ep)
{
	WD_Crash((EXCEPTION_POINTERS *)ep);
}

static void
WD_Dump(DWORD mainid, LONG stalledms)
{
	HANDLE process = GetCurrentProcess();
	HANDLE snap;
	THREADENTRY32 te;
	FILE *f = fopen(wd_path, "a");

	if (!f)
	{
		return;
	}

	fprintf(f, "=== stall: no heartbeat for %ld ms, last at \"%s\"\n", (long)stalledms, (const char *)wd_where);
	pSymRefreshModuleList(process);
	WD_DumpThread(f, process, mainid, true);

	snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

	if (snap != INVALID_HANDLE_VALUE)
	{
		te.dwSize = sizeof(te);

		if (Thread32First(snap, &te))
		{
			do
			{
				if ((te.th32OwnerProcessID == GetCurrentProcessId()) && (te.th32ThreadID != mainid) &&
						(te.th32ThreadID != GetCurrentThreadId()))
				{
					WD_DumpThread(f, process, te.th32ThreadID, false);
				}
			}
			while (Thread32Next(snap, &te));
		}

		CloseHandle(snap);
	}

	SV_ImportTrace_Dump(f);
	fprintf(f, "=== end\n");
	fclose(f);
}

static DWORD WINAPI
WD_Thread(LPVOID arg)
{
	DWORD mainid = (DWORD)(UINT_PTR)arg;
	LONG last = wd_beat;
	DWORD since = GetTickCount();
	qboolean reported = false;

	for (;;)
	{
		LONG limit = (LONG)(sv_watchdog->value * 1000.0f);

		Sleep(250);

		if (wd_beat != last)
		{
			last = wd_beat;
			since = GetTickCount();
			reported = false;
			continue;
		}

		if ((limit > 0) && wd_armed && !reported && ((LONG)(GetTickCount() - since) > limit))
		{
			WD_Dump(mainid, (LONG)(GetTickCount() - since));
			reported = true;
		}
	}

	return 0;
}

void
SV_Watchdog_Init(void)
{
	HMODULE dbg;

	sv_watchdog = Cvar_Get("sv_watchdog", "0", 0);

	if (wd_thread || (sv_watchdog->value <= 0))
	{
		return;
	}

	/* the Debugging Tools' DbgHelp can fetch system symbols (symsrv.dll, with
	   _NT_SYMBOL_PATH set), so stacks walk through system DLL frames */
#ifdef _M_IX86
	dbg = LoadLibraryA("C:/Program Files (x86)/Windows Kits/10/Debuggers/x86/dbghelp.dll");
#else
	dbg = LoadLibraryA("C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/dbghelp.dll");
#endif

	if (!dbg)
	{
		dbg = LoadLibraryA("dbghelp.dll");
	}

	if (!dbg)
	{
		return;
	}

	pSymInitialize = (SymInitialize_t)(void *)GetProcAddress(dbg, "SymInitialize");
	pSymSetOptions = (SymSetOptions_t)(void *)GetProcAddress(dbg, "SymSetOptions");
	pSymFromAddr = (SymFromAddr_t)(void *)GetProcAddress(dbg, "SymFromAddr");
	pSymGetLineFromAddr64 = (SymGetLineFromAddr64_t)(void *)GetProcAddress(dbg, "SymGetLineFromAddr64");
	pStackWalk64 = (StackWalk64_t)(void *)GetProcAddress(dbg, "StackWalk64");
	pSymRefreshModuleList = (SymRefreshModuleList_t)(void *)GetProcAddress(dbg, "SymRefreshModuleList");
	pSymFunctionTableAccess64 = (PVOID)GetProcAddress(dbg, "SymFunctionTableAccess64");
	pSymGetModuleBase64 = (PVOID)GetProcAddress(dbg, "SymGetModuleBase64");

	if (!pSymInitialize || !pSymSetOptions || !pSymFromAddr || !pSymGetLineFromAddr64 || !pStackWalk64 ||
			!pSymRefreshModuleList || !pSymFunctionTableAccess64 || !pSymGetModuleBase64)
	{
		return;
	}

	pSymSetOptions(0x00000002 /* SYMOPT_UNDNAME */ | 0x00000010 /* SYMOPT_LOAD_LINES */ |
			0x00000004 /* SYMOPT_DEFERRED_LOADS */);
	pSymInitialize(GetCurrentProcess(), NULL, TRUE);

	Com_sprintf(wd_path, sizeof(wd_path), "%s/watchdog.txt", FS_Userdir());
	wd_thread = CreateThread(NULL, 0, WD_Thread, (LPVOID)(UINT_PTR)GetCurrentThreadId(), 0, NULL);
	/* the report goes along with the always-on crash report (backends/windows/crash.c) */
	Sys_Crash_SetReporter(WD_CrashReport);
	Sys_Crash_Quiet(true);
	Com_Printf("sv_watchdog: stall reports to %s after %g s\n", wd_path, sv_watchdog->value);
}

#else

void SV_Watchdog_Init(void) {}
void SV_Watchdog_Beat(const char *where) { (void)where; }
void SV_Watchdog_Mark(const char *where) { (void)where; }
void SV_Watchdog_Disarm(void) {}

#endif
