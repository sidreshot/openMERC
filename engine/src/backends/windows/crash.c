/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc Windows crash report (a diagnostic, not in the original). Always on:
 * a fault anywhere in the process (access violation, stack overflow, ...), a CRT
 * invalid-parameter or pure-virtual call, or abort() writes
 *   <user>/crash/crash-<date>_<time>.dmp  (minidump: threads, stacks, modules)
 *   <user>/crash/crash-<date>_<time>.txt  (exception, module, stack of OpenMerc
 *                                          modules, last game imports)
 * then flushes stdout, sof.log and the om_autorecord JSONL (with an "end" event
 * "crash ...") before the process ends. <user> is the portable log folder
 * (<exe>user) or the home folder until the file system is up, then the
 * game's user folder (FS_Userdir). The filter is put back every frame in case a
 * module (the CRT, the original game modules) replaced it. Offsets are written
 * for OpenMerc's own modules (DYNAMICBASE images in the game folder); for the
 * original modules and system DLLs only the module name (clean-room rule, as in
 * sv_watchdog.c) - the minidump holds the rest for a debugger.
 *
 * Implements: SPEC-001 host diagnostics (no game behaviour)
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#include "../../common/header/common.h"
#include "../../common/header/om_version.h"

typedef BOOL (WINAPI *MiniDumpWriteDump_t)(HANDLE, DWORD, HANDLE, int, void *, void *, void *);

typedef struct
{
	DWORD ThreadId;
	EXCEPTION_POINTERS *ExceptionPointers;
	BOOL ClientPointers;
} om_mdei_t;

static MiniDumpWriteDump_t pMiniDumpWriteDump;
static char crash_dir[MAX_OSPATH];
static char crash_gamedir[MAX_OSPATH];
static volatile LONG crash_busy;
static qboolean crash_quiet;
static qboolean crash_replaced_said;
static void (*crash_reporter)(void *ep);
static EXCEPTION_POINTERS *crash_ep;
static DWORD crash_thread;
static const char *crash_kind;

void SV_ImportTrace_Dump(void *file); /* sv_imptrace.c */
void Com_CrashFlush(const char *reason); /* clientserver.c */

void
Sys_Crash_SetReporter(void (*fn)(void *ep))
{
	crash_reporter = fn;
}

void
Sys_Crash_Quiet(qboolean quiet)
{
	crash_quiet = quiet;
}

/* module of an address; *own: an OpenMerc-built image (DYNAMICBASE, in the game folder) */
static HMODULE
Crash_Module(const void *addr, char *name, size_t size, qboolean *own)
{
	HMODULE h = NULL;
	char path[MAX_PATH];

	name[0] = 0;
	*own = false;

	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)addr, &h) || !h)
	{
		return NULL;
	}

	if (GetModuleFileNameA(h, path, sizeof(path)))
	{
		const char *s = strrchr(path, '\\');
		IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)h;
		IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((char *)h + dos->e_lfanew);

		Q_strlcpy(name, s ? s + 1 : path, size);
		*own = (_strnicmp(path, crash_gamedir, strlen(crash_gamedir)) == 0) &&
			(nt->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE);
	}

	return h;
}

/* inside executable code (return addresses, not data or headers) */
static qboolean
Crash_IsCode(const void *addr)
{
	MEMORY_BASIC_INFORMATION mbi;

	return VirtualQuery(addr, &mbi, sizeof(mbi)) && (mbi.State == MEM_COMMIT) &&
		(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}

static void
Crash_PrintAddr(FILE *f, const char *what, const void *addr)
{
	char name[64];
	qboolean own;
	HMODULE h = Crash_Module(addr, name, sizeof(name), &own);

	if (!h)
	{
		fprintf(f, "%s(no module)\n", what);
	}
	else if (own)
	{
		fprintf(f, "%s%s+0x%lx\n", what, name, (unsigned long)((ULONG_PTR)addr - (ULONG_PTR)h));
	}
	else
	{
		fprintf(f, "%s%s (not an OpenMerc module: offset in the .dmp only)\n", what, name);
	}
}

static DWORD WINAPI
Crash_Writer(LPVOID arg)
{
	EXCEPTION_POINTERS *ep = crash_ep;
	SYSTEMTIME st;
	char base[MAX_OSPATH], path[MAX_OSPATH];
	FILE *f;
	DWORD code = ep ? ep->ExceptionRecord->ExceptionCode : 0;

	(void)arg;
	GetLocalTime(&st);
	CreateDirectoryA(crash_dir, NULL);
	snprintf(base, sizeof(base), "%scrash-%04d%02d%02d_%02d%02d%02d", crash_dir, st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond);

	if (pMiniDumpWriteDump)
	{
		HANDLE fh;

		snprintf(path, sizeof(path), "%s.dmp", base);
		fh = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		if (fh != INVALID_HANDLE_VALUE)
		{
			om_mdei_t mi;

			mi.ThreadId = crash_thread;
			mi.ExceptionPointers = ep;
			mi.ClientPointers = FALSE;
			/* MiniDumpNormal | WithIndirectlyReferencedMemory | WithUnloadedModules | WithThreadInfo */
			pMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), fh, 0x0040 | 0x0020 | 0x1000,
					ep ? &mi : NULL, NULL, NULL);
			CloseHandle(fh);
		}
	}

	snprintf(path, sizeof(path), "%s.txt", base);
	f = fopen(path, "w");

	if (f)
	{
		fprintf(f, "openMERC crash report: %s, build %s\n", crash_kind, OM_BUILD);
		fprintf(f, "time %04d-%02d-%02d %02d:%02d:%02d, exception %08lx\n", st.wYear, st.wMonth, st.wDay,
				st.wHour, st.wMinute, st.wSecond, (unsigned long)code);

		if (ep)
		{
			const EXCEPTION_RECORD *er = ep->ExceptionRecord;

			if ((er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) && (er->NumberParameters >= 2))
			{
				fprintf(f, "%s of address %p\n", er->ExceptionInformation[0] == 0 ? "read" :
						(er->ExceptionInformation[0] == 1 ? "write" : "execute"),
						(void *)er->ExceptionInformation[1]);
			}

			Crash_PrintAddr(f, "in ", er->ExceptionAddress);
#ifdef _M_IX86
			{
				/* return addresses into OpenMerc modules on the faulting stack */
				ULONG_PTR sp = ep->ContextRecord->Esp, top;
				MEMORY_BASIC_INFORMATION mbi;
				int n = 0;

				if (VirtualQuery((LPCVOID)sp, &mbi, sizeof(mbi)) && (mbi.State == MEM_COMMIT))
				{
					top = (ULONG_PTR)mbi.BaseAddress + mbi.RegionSize;

					if (top > sp + 0x10000)
					{
						top = sp + 0x10000;
					}

					fprintf(f, "stack (OpenMerc modules; the rest by name):\n");

					for (; (sp + 4 <= top) && (n < 48); sp += 4)
					{
						const void *v = *(const void **)sp;
						char name[64];
						qboolean own;
						HMODULE h;

						if ((ULONG_PTR)v < 0x10000)
						{
							continue;
						}

						h = Crash_Module(v, name, sizeof(name), &own);

						if (h && own && Crash_IsCode(v))
						{
							fprintf(f, "  %s+0x%lx\n", name, (unsigned long)((ULONG_PTR)v - (ULONG_PTR)h));
							n++;
						}
					}
				}
			}
#endif
		}

		SV_ImportTrace_Dump(f);
		fprintf(f, "files: %s.dmp / .txt\n", base);
		fclose(f);
	}

	if (crash_reporter && ep)
	{
		crash_reporter(ep); /* sv_watchdog: full stack walk to watchdog.txt */
	}

	Com_Printf("OMCRASH: %s %08lx, report %s.txt\n", crash_kind, (unsigned long)code, base);
	{
		char reason[96];

		snprintf(reason, sizeof(reason), "crash %s %08lx", crash_kind, (unsigned long)code);
		Com_CrashFlush(reason);
	}

	return 0;
}

/* writes the report from a fresh thread (enough stack after a stack overflow) */
static void
Crash_Report(EXCEPTION_POINTERS *ep, const char *kind)
{
	HANDLE th;

	if (InterlockedExchange(&crash_busy, 1))
	{
		return; /* a second fault while reporting */
	}

	crash_ep = ep;
	crash_kind = kind;
	crash_thread = GetCurrentThreadId();
	th = CreateThread(NULL, 256 * 1024, Crash_Writer, NULL, 0, NULL);

	if (th)
	{
		WaitForSingleObject(th, 20000);
		CloseHandle(th);
	}
}

static LONG WINAPI
Crash_Filter(EXCEPTION_POINTERS *ep)
{
	Crash_Report(ep, "unhandled exception");
	/* test runs (sv_watchdog) end quietly; a player gets the system's error message */
	return crash_quiet ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

/* CRT error paths: a report with the caller's context, then the end of the process */
static void
Crash_Raise(DWORD code, const char *kind)
{
	__try
	{
		RaiseException(code, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}
	__except (Crash_Report(GetExceptionInformation(), kind), EXCEPTION_EXECUTE_HANDLER)
	{
	}

	TerminateProcess(GetCurrentProcess(), 3);
}

static void __cdecl
Crash_InvalidParameter(const wchar_t *expr, const wchar_t *func, const wchar_t *file, unsigned line,
		uintptr_t reserved)
{
	(void)expr; (void)func; (void)file; (void)line; (void)reserved;
	Crash_Raise(0xE04F4D01, "CRT invalid parameter");
}

static void __cdecl
Crash_Purecall(void)
{
	Crash_Raise(0xE04F4D02, "pure virtual call");
}

static void __cdecl
Crash_Abort(int sig)
{
	(void)sig;
	Crash_Raise(0xE04F4D03, "abort()");
}

void
Sys_Crash_Install(const char *userdir)
{
	HMODULE dbg;
	char *s;

	if (!GetModuleFileNameA(NULL, crash_gamedir, sizeof(crash_gamedir)) ||
			!(s = strrchr(crash_gamedir, '\\')))
	{
		crash_gamedir[0] = 0;
	}
	else
	{
		s[1] = 0;
	}

	snprintf(crash_dir, sizeof(crash_dir), "%scrash\\", userdir);

	/* loaded now: no loader work inside the fault */
	dbg = LoadLibraryA("dbghelp.dll");

	if (dbg)
	{
		pMiniDumpWriteDump = (MiniDumpWriteDump_t)(void *)GetProcAddress(dbg, "MiniDumpWriteDump");
	}

	SetUnhandledExceptionFilter(Crash_Filter);
	_set_invalid_parameter_handler(Crash_InvalidParameter);
	_set_purecall_handler(Crash_Purecall);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	signal(SIGABRT, Crash_Abort);
}

/* test command: om_crashtest <null|av|stack|abort|purecall|invparam>, needs cheats 1 */
static int
Crash_Recurse(volatile int *p)
{
	volatile char pad[4096];

	pad[0] = (char)*p;
	return Crash_Recurse(p) + pad[0];
}

static void
Crash_Test_f(void)
{
	const char *k = Cmd_Argv(1);

	if (!Cvar_VariableValue("cheats"))
	{
		Com_Printf("om_crashtest needs cheats 1\n");
		return;
	}

	Com_Printf("om_crashtest %s\n", k);

	if (!strcmp(k, "null") || !strcmp(k, "av"))
	{
		volatile int *volatile z = NULL;

		*z = 1;
	}
	else if (!strcmp(k, "stack"))
	{
		volatile int v = 0;

		Crash_Recurse(&v);
	}
	else if (!strcmp(k, "abort"))
	{
		abort();
	}
	else if (!strcmp(k, "purecall"))
	{
		Crash_Purecall();
	}
	else if (!strcmp(k, "invparam"))
	{
		char *volatile np = NULL;

		strcpy_s(np, 4, "x");
	}
	else
	{
		Com_Printf("usage: om_crashtest <null|stack|abort|purecall|invparam>\n");
	}
}

/* once a frame: put the filter back if a module replaced it */
void
Sys_Crash_Frame(void)
{
	static qboolean cmd_added;
	LPTOP_LEVEL_EXCEPTION_FILTER prev = SetUnhandledExceptionFilter(Crash_Filter);

	if (!cmd_added)
	{
		cmd_added = true;
		Cmd_AddCommand("om_crashtest", Crash_Test_f);
	}

	/* the game's user folder (the `user` cvar, e.g. <exe>user) once the file
	   system is up; until then the log folder given to Sys_Crash_Install */
	{
		static char last[MAX_OSPATH];
		const char *u = FS_Userdir();

		if (u && u[0] && strcmp(u, last))
		{
			Q_strlcpy(last, u, sizeof(last));
			snprintf(crash_dir, sizeof(crash_dir), "%s/crash/", u);
		}
	}

	if ((prev != Crash_Filter) && !crash_replaced_said)
	{
		char name[64];
		qboolean own;

		crash_replaced_said = true;
		name[0] = 0;

		if (prev)
		{
			Crash_Module((const void *)prev, name, sizeof(name), &own);
		}

		Com_Printf("crash report: exception filter %s was replaced, restored\n", prev ? name : "(none)");
	}
}